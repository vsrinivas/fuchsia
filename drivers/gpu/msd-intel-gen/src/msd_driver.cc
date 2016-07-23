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

#include "msd_driver.h"
#include "magma_util/dlog.h"
#include "msd_device.h"

MsdDriver::MsdDriver() { magic_ = kMagic; }

MsdDriver* MsdDriver::Create()
{
    auto drv = new MsdDriver();
    if (!drv) {
        DLOG("Failed to allocate MsdDriver");
        return nullptr;
    }
    return drv;
}

void MsdDriver::Destroy(MsdDriver* drv) { delete drv; }

MsdDevice* MsdDriver::CreateDevice(void* device)
{
    auto dev = new MsdDevice();
    if (!dev) {
        DLOG("Failed to allocate MsdDevice");
        return nullptr;
    }
    return dev;
}

void MsdDriver::DestroyDevice(MsdDevice* dev) { delete dev; }

//////////////////////////////////////////////////////////////////////////////

msd_driver* msd_driver_create(void) { return MsdDriver::Create(); }

void msd_driver_destroy(msd_driver* drv) { MsdDriver::Destroy(MsdDriver::cast(drv)); }

msd_device* msd_driver_create_device(msd_driver* drv, void* device)
{
    return MsdDriver::cast(drv)->CreateDevice(device);
}

void msd_driver_destroy_device(msd_driver* drv, msd_device* dev)
{
    MsdDriver::cast(drv)->DestroyDevice(MsdDevice::cast(dev));
}
