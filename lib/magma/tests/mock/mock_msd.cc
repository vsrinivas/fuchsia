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

#include "mock_msd.h"
#include "msd.h"

struct msd_driver* msd_driver_create(void) { return new MsdMockDriver(); }

void msd_driver_destroy(msd_driver* drv) { delete MsdMockDriver::cast(drv); }

msd_device* msd_driver_create_device(msd_driver* drv, void* device)
{
    // If youre passing something meaningful in here youre #doingitwrong
    DASSERT(!device);

    return MsdMockDriver::cast(drv)->CreateDevice();
}

void msd_driver_destroy_device(msd_device* dev)
{
    // TODO(MA-28) should be
    // MsdMockDriver::cast(drv)->DestroyDevice(MsdMockDevice::cast(dev));
    delete MsdMockDevice::cast(dev);
}

int32_t msd_device_open(msd_device* dev, msd_client_id client_id)
{
    return MsdMockDevice::cast(dev)->Open(client_id);
}

int32_t msd_device_close(msd_device* dev, msd_client_id client_id)
{
    return MsdMockDevice::cast(dev)->Close(client_id);
}

uint32_t msd_device_get_id(msd_device* dev) { return MsdMockDevice::cast(dev)->GetDeviceId(); }
