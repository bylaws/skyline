// SPDX-License-Identifier: MIT or MPL-2.0
// Copyright © 2021 Skyline Team and Contributors (https://github.com/skyline-emu/)
// Copyright © 2019-2020 Ryujinx Team and Contributors

#include <soc.h>
#include <kernel/types/KProcess.h>
#include <services/nvdrv/devices/deserialisation/deserialisation.h>
#include "ctrl.h"

namespace skyline::service::nvdrv::device::nvhost {
    Ctrl::SyncpointEvent::SyncpointEvent(const DeviceState &state) : event(std::make_shared<type::KEvent>(state, false)) {}

    void Ctrl::SyncpointEvent::Signal() {
        // We should only signal the KEvent if the event is actively being waited on
        if (state.exchange(State::Signalling) == State::Waiting)
            event->Signal();

        state = State::Signalled;
    }

    void Ctrl::SyncpointEvent::Cancel(soc::host1x::Host1X &host1x) {
        host1x.syncpoints.at(fence.id).DeregisterWaiter(waiterId);
    }

    void Ctrl::SyncpointEvent::RegisterWaiter(soc::host1x::Host1X &host1x, const Fence &pFence) {
        fence = pFence;
        state = State::Waiting;
        waiterId = host1x.syncpoints.at(fence.id).RegisterWaiter(fence.value, [this] { Signal(); });
    }

    bool Ctrl::SyncpointEvent::IsInUse() {
        return state == SyncpointEvent::State::Waiting ||
            state == SyncpointEvent::State::Cancelling ||
            state == SyncpointEvent::State::Signalling;
    }

    Ctrl::Ctrl(const DeviceState &state, Core &core, const SessionContext &ctx) : NvDevice(state, core, ctx) {}

    u32 Ctrl::FindFreeSyncpointEvent(u32 syncpointId) {
        u32 eventSlot{SyncpointEventCount}; //!< Holds the slot of the last populated event in the event array
        u32 freeSlot{SyncpointEventCount}; //!< Holds the slot of the first unused event id

        for (u32 i{}; i < SyncpointEventCount; i++) {
            if (syncpointEvents[i]) {
                const auto &event{syncpointEvents[i]};

                if (!event->IsInUse()) {
                    eventSlot = i;

                    // This event is already attached to the requested syncpoint, so use it
                    if (event->fence.id == syncpointId)
                        return eventSlot;
                }
            } else if (freeSlot == SyncpointEventCount) {
                freeSlot = i;
            }
        }

        // Use an unused event if possible
        if (freeSlot < SyncpointEventCount) {
            syncpointEvents[freeSlot] = std::make_unique<SyncpointEvent>(state);
            return freeSlot;
        }

        // Recycle an existing event if all else fails
        if (eventSlot < SyncpointEventCount)
            return eventSlot;

        throw exception("Failed to find a free nvhost event!");
    }

    PosixResult Ctrl::SyncpointWaitEventImpl(In<Fence> fence, In<i32> timeout, InOut<SyncpointEventValue> value, bool allocate) {
        if (fence.id >= soc::host1x::SyncpointCount)
            return PosixResult::InvalidArgument;

        // Check if the syncpoint has already expired using the last known values
        if (core.syncpointManager.HasSyncpointExpired(fence.id, fence.value)) {
            value.val = core.syncpointManager.ReadSyncpointMinValue(fence.id);
            return PosixResult::Success;
        }

        // Sync the syncpoint with the GPU then check again
        auto minVal{core.syncpointManager.UpdateMin(fence.id)};
        if (core.syncpointManager.HasSyncpointExpired(fence.id, fence.value)) {
            value.val = minVal;
            return PosixResult::Success;
        }

        // Don't try to register any waits if there is no timeout for them
        if (!timeout)
            return PosixResult::TryAgain;

        std::lock_guard lock(syncpointEventMutex);

        u32 slot = [&]() {
            if (allocate) {
                value.val = 0;
                return FindFreeSyncpointEvent(fence.id);
            } else {
                return value.val;
            }
        }();

        if (slot >= SyncpointEventCount)
            return PosixResult::InvalidArgument;

        auto &event{syncpointEvents[slot]};
        if (!event)
            return PosixResult::InvalidArgument;

        if (!event->IsInUse()) {
            state.logger->Debug("Waiting on syncpoint event: {} with fence: ({}, {})", slot, fence.id, fence.value);
            event->RegisterWaiter(state.soc->host1x, fence);

            value.val = 0;

            if (allocate) {
                value.syncpointIdForAllocation = fence.id;
                value.eventAllocated = true;
            } else {
                value.syncpointId = fence.id;
            }

            // Slot will overwrite some of syncpointId here... it makes no sense for Nvidia to do this
            value.val |= slot;

            return PosixResult::TryAgain;
        } else {
            return PosixResult::InvalidArgument;
        }
    }

    PosixResult Ctrl::SyncpointFreeEventLocked(In<u32> slot) {
        if (slot >= SyncpointEventCount)
            return PosixResult::InvalidArgument;

        auto &event{syncpointEvents[slot]};
        if (!event)
            return PosixResult::Success; // If the event doesn't already exist then we don't need to do anything

        if (event->IsInUse()) // Avoid freeing events when they are still waiting etc.
            return PosixResult::Busy;

        syncpointEvents[slot] = nullptr;

        return PosixResult::Success;
    }

    PosixResult Ctrl::SyncpointClearEventWait(In<SyncpointEventValue> value) {
        u16 slot{value.slot};
        if (slot >= SyncpointEventCount)
            return PosixResult::InvalidArgument;

        std::lock_guard lock(syncpointEventMutex);

        auto &event{syncpointEvents[slot]};
        if (!event)
            return PosixResult::InvalidArgument;

        if (event->state.exchange(SyncpointEvent::State::Cancelling) == SyncpointEvent::State::Waiting) {
            state.logger->Debug("Cancelling waiting syncpoint event: {}", slot);
            event->Cancel(state.soc->host1x);
            core.syncpointManager.UpdateMin(event->fence.id);
        }

        event->state = SyncpointEvent::State::Cancelled;
        event->event->ResetSignal();

        return PosixResult::Success;
    }

    PosixResult Ctrl::SyncpointWaitEvent(In<Fence> fence, In<i32> timeout, InOut<SyncpointEventValue> value) {
        return SyncpointWaitEventImpl(fence, timeout, value, true);
    }

    PosixResult Ctrl::SyncpointWaitEventSingle(In<Fence> fence, In<i32> timeout, InOut<SyncpointEventValue> value) {
        return SyncpointWaitEventImpl(fence, timeout, value, false);
    }

    PosixResult Ctrl::SyncpointAllocateEvent(In<u32> slot) {
        state.logger->Debug("Registering syncpoint event: {}", slot);

        if (slot >= SyncpointEventCount)
            return PosixResult::InvalidArgument;

        std::lock_guard lock(syncpointEventMutex);

        auto &event{syncpointEvents[slot]};
        if (event)
            if (auto err{SyncpointFreeEventLocked(slot)}; err != PosixResult::Success)
                return err;

        event = std::make_unique<SyncpointEvent>(state);

        return PosixResult::Success;
    }

    PosixResult Ctrl::SyncpointFreeEvent(In<u32> slot) {
        std::lock_guard lock(syncpointEventMutex);
        return SyncpointFreeEventLocked(slot);
    }

    PosixResult Ctrl::SyncpointFreeEventBatch(In<u64> bitmask) {
        auto err{PosixResult::Success};

        // Avoid repeated locks/unlocks by just locking now
        std::lock_guard lock(syncpointEventMutex);

        for (int i{}; i < 64; i++) {
            if (bitmask & (1 << i))
                if (auto freeErr{SyncpointFreeEventLocked(i)}; freeErr != PosixResult::Success)
                    err = freeErr;
        }

        return err;
    }

    std::shared_ptr<type::KEvent> Ctrl::QueryEvent(u32 valueRaw) {
        SyncpointEventValue value{.val = valueRaw};

        // I have no idea why nvidia does this
        u16 slot{value.eventAllocated ? static_cast<u16>(value.partialSlot) : value.slot};
        if (slot >= SyncpointEventCount)
            return nullptr;

        u32 syncpointId{value.eventAllocated ? static_cast<u32>(value.syncpointIdForAllocation) : value.syncpointId};

        std::lock_guard lock(syncpointEventMutex);

        auto &event{syncpointEvents[slot]};
        if (event && event->fence.id == syncpointId)
            return event->event;

        return nullptr;
    }

#include <services/nvdrv/devices/deserialisation/macro_def.h>
    static constexpr u32 CtrlMagic{0};

    IOCTL_HANDLER_FUNC(Ctrl, ({
        IOCTL_CASE_ARGS(INOUT, SIZE(0x4),  MAGIC(CtrlMagic), FUNC(0x1C),
                        SyncpointClearEventWait,  ARGS(In<SyncpointEventValue>))
        IOCTL_CASE_ARGS(INOUT, SIZE(0x10), MAGIC(CtrlMagic), FUNC(0x1D),
                        SyncpointWaitEvent,       ARGS(In<Fence>, In<i32>, InOut<SyncpointEventValue>))
        IOCTL_CASE_ARGS(INOUT, SIZE(0x10), MAGIC(CtrlMagic), FUNC(0x1E),
                        SyncpointWaitEventSingle, ARGS(In<Fence>, In<i32>, InOut<SyncpointEventValue>))
        IOCTL_CASE_ARGS(INOUT, SIZE(0x4),  MAGIC(CtrlMagic), FUNC(0x1F),
                        SyncpointAllocateEvent,   ARGS(In<u32>))
        IOCTL_CASE_ARGS(INOUT, SIZE(0x4),  MAGIC(CtrlMagic), FUNC(0x20),
                        SyncpointFreeEvent,       ARGS(In<u32>))
        IOCTL_CASE_ARGS(INOUT, SIZE(0x8),  MAGIC(CtrlMagic), FUNC(0x21),
                        SyncpointFreeEventBatch,  ARGS(In<u64>))

        IOCTL_CASE_RESULT(INOUT, SIZE(0x183), MAGIC(CtrlMagic), FUNC(0x1B),
                          PosixResult::InvalidArgument) // GetConfig isn't available in production
    }))
#include <services/nvdrv/devices/deserialisation/macro_undef.h>
}
