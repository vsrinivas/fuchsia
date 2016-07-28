// Copyright 2016 The Fuchsia Authors
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef MAGENTA_PLATFORM_DEVICE_H
#define MAGENTA_PLATFORM_DEVICE_H

#include "platform_device.h"

#include <ddk/device.h>

namespace magma {

class MagentaPlatformDevice : public PlatformDevice {
public:
    MagentaPlatformDevice(mx_device_t* mx_device) : mx_device_(mx_device) {}

    std::unique_ptr<PlatformMmio> CpuMapPciMmio(unsigned int pci_bar,
                                                PlatformMmio::CachePolicy cache_policy) override;

private:
    mx_device_t* mx_device() { return mx_device_; }

    mx_device_t* mx_device_;
};

} // namespace

#endif // MAGENTA_PLATFORM_DEVICE_H
