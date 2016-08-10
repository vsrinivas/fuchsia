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

std::unique_ptr<MsdConnection> MsdDevice::Open(msd_client_id client_id)
{
    return std::unique_ptr<MsdConnection>(new MsdConnection());
}

//////////////////////////////////////////////////////////////////////////////

msd_connection* msd_device_open(msd_device* dev, msd_client_id client_id)
{
    // here we open the connection and transfer ownership of the result across the ABI
    return MsdDevice::cast(dev)->Open(client_id).release();
}

uint32_t msd_device_get_id(msd_device* dev) { return MsdDevice::cast(dev)->device_id(); }
