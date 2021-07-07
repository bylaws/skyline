// SPDX-License-Identifier: MPL-2.0
// Copyright © 2020 Skyline Team and Contributors (https://github.com/skyline-emu/)

#include "driver.h"
#include "devices/nvhost_ctrl.h"
#include "devices/nvhost_ctrl_gpu.h"
#include "devices/nvmap.h"
#include "devices/nvhost_channel.h"
#include "devices/nvhost_as_gpu.h"

namespace skyline::service::nvdrv {
    Driver::Driver(const DeviceState &state) : state(state) {}

    NvResult Driver::OpenDevice(std::string_view path, FileDescriptor fd, const SessionContext &ctx) {
        state.logger->Debug("Opening NvDrv device ({}): {}", fd, path);
        auto pathHash{util::Hash(path)};

        #define ENTRY(path, object) \
            case util::Hash(path): \
                devices.emplace(fd, std::make_unique<device::object>(state, ctx)); \
                return state::Success;

        switch (pathHash) {
            ENTRY("/dev/nvmap", nvmap::NvMap)
            ENTRY("/dev/nvhost-ctrl", nvhost::Ctrl)
            default:
                break;
        }

        if (ctx.perms.AccessGpu) {
            switch (pathHash) {
                ENTRY("/dev/nvhost-as-gpu", nvhost::AsGpu)
                ENTRY("/dev/nvhost-ctrl-gpu", nvhost::Ctrl)
                ENTRY("/dev/nvhost-gpu", nvhost::Gpu)
                default:
                    break;
            }
        }

        if (ctx.perms.AccessVic) {
            switch (pathHash) {
                ENTRY("/dev/nvhost-vic", nvhost::Vic)
                default:
                    break;
            }
        }

        if (ctx.perms.AccessVideoDecoder) {
            switch (pathHash) {
                ENTRY("/dev/nvhost-nvdec", nvhost::NvDec)
                default:
                    break;
            }
        }

        if (ctx.perms.AccessJpeg) {
            switch (pathHash) {
                ENTRY("/dev/nvhost-nvjpg", nvhost::NvJpg)
                default:
                    break;
            }
        }

        // Device doesn't exist/no permissions
        return NvError::FileOperationFailed;
    }

    std::shared_ptr<device::NvDevice> Driver::GetDevice(u32 fd) {
        try {
            auto item{devices.at(fd)};
            if (!item)
                throw exception("GetDevice was called with a closed file descriptor: 0x{:X}", fd);
            return item;
        } catch (std::out_of_range) {
            throw exception("GetDevice was called with invalid file descriptor: 0x{:X}", fd);
        }
    }

    void Driver::CloseDevice(u32 fd) {
        try {
            auto &device{devices.at(fd)};
            device.reset();
        } catch (const std::out_of_range &) {
            state.logger->Warn("Trying to close non-existent FD");
        }
    }

    std::weak_ptr<Driver> driver{};
}
