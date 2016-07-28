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

#include "msd_device.h"
#include "magma_util/dlog.h"
#include "magma_util/macros.h"
#include <errno.h>

MsdDevice::MsdDevice() { magic_ = kMagic; }

int32_t MsdDevice::Open(msd_client_id client_id)
{
    DLOG("TODO: Open");
    return DRET(-EINVAL);
}

int32_t MsdDevice::Close(msd_client_id client_id)
{
    DLOG("TODO: Close");
    return DRET(-EINVAL);
}

//////////////////////////////////////////////////////////////////////////////

int32_t msd_device_open(msd_device* dev, msd_client_id client_id)
{
    return MsdDevice::cast(dev)->Open(client_id);
}

int32_t msd_device_close(msd_device* dev, msd_client_id client_id)
{
    MsdDevice::cast(dev)->Close(client_id);
    return 0;
}

uint32_t msd_device_get_id(msd_device* dev) { return MsdDevice::cast(dev)->device_id(); }
