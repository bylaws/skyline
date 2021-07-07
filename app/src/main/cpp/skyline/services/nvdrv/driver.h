// SPDX-License-Identifier: MPL-2.0
// Copyright © 2020 Skyline Team and Contributors (https://github.com/skyline-emu/)

#pragma once

#include <common.h>
#include "types.h"
#include "devices/nvdevice.h"
#include "core/core.h"

namespace skyline::service::nvdrv {

    class Driver {
      private:
        const DeviceState &state;
        std::unordered_map<FileDescriptor, std::unique_ptr<device::NvDevice>> devices;

      public:
        Driver(const DeviceState &state);

        Core core;


        NvResult OpenDevice(std::string_view path, FileDescriptor fd, const SessionContext &ctx);

        /**
         * @brief Returns a particular device with a specific FD
         * @param fd The file descriptor to retrieve
         * @return A shared pointer to the device
         */
        std::shared_ptr<device::NvDevice> GetDevice(u32 fd);

        /**
         * @brief Closes the specified device with its file descriptor
         */
        void CloseDevice(u32 fd);
    };
}
