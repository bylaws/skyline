// SPDX-License-Identifier: MPL-2.0
// Copyright © 2020 Skyline Team and Contributors (https://github.com/skyline-emu/)

#pragma once

#include "nvdevice.h"

namespace skyline::service::nvdrv::device {
    using NvMapCore = core::NvMap;

    /**
     * @brief NvMap (/dev/nvmap) is used to keep track of buffers and map them onto the SMMU (https://switchbrew.org/wiki/NV_services)
     * @url https://android.googlesource.com/kernel/tegra/+/refs/heads/android-tegra-flounder-3.10-marshmallow/include/linux/nvmap.h
     */
    class NvMap : public NvDevice {
      public:
        enum class HandleParameterType : u32 {
            Size = 1,
            Alignment = 2,
            Base = 3,
            Heap = 4,
            Kind = 5,
            IsSharedMemMapped = 6
        };

        NvMap(const DeviceState &state, Core &core, const SessionContext &ctx);

        PosixResult Ioctl(IoctlDescriptor cmd, span<u8> buffer) override;

        /**
         * @brief Creates an nvmap handle for the given size
         * @url https://switchbrew.org/wiki/NV_services#NVMAP_IOC_CREATE
         */
        PosixResult Create(In<u32> size, Out<NvMapCore::Handle::Id> handle);

        PosixResult FromId(In<NvMapCore::Handle::Id> id, Out<NvMapCore::Handle::Id> handle);

        PosixResult Alloc(In<NvMapCore::Handle::Id> handle, In<u32> heapMask, In<NvMapCore::Handle::Flags> flags, InOut<u32> align, In<u8> kind, In<u64> address);

        PosixResult Free(In<NvMapCore::Handle::Id> handle, Out<u64> address, Out<u32> size, Out<NvMapCore::Handle::Flags> flags);

        PosixResult Param(In<NvMapCore::Handle::Id> handle, In<HandleParameterType> param, Out<u32> result);

        PosixResult GetId(Out<NvMapCore::Handle::Id> id, In<NvMapCore::Handle::Id> handle);
    };
}
