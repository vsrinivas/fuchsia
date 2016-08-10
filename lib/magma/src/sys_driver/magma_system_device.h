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

#ifndef _MAGMA_SYSTEM_DEVICE_H_
#define _MAGMA_SYSTEM_DEVICE_H_
#include "msd.h"
#include <functional>
#include <memory>

class MagmaSystemConnection;

using msd_device_unique_ptr_t = std::unique_ptr<msd_device, std::function<void(msd_device*)>>;

class MagmaSystemDevice {
public:
    MagmaSystemDevice(msd_device_unique_ptr_t msd_dev) : msd_dev_(std::move(msd_dev)) {}

    // Opens a connection to the device. This transfers ownership of this object to the
    // caller for now, since that is the semantics of what will happen when connections are message
    // pipes and the caller is another process. There will probably need to be a close function at
    // that point but we dont need it now so I havent included it
    // Close this connection by deleting the returned object
    // returns nullptr on failure
    std::unique_ptr<MagmaSystemConnection> Open(msd_client_id client_id);

    msd_device* arch() { return msd_dev_.get(); }

    // Returns the device id. 0 is invalid.
    uint32_t GetDeviceId();

private:
    msd_device_unique_ptr_t msd_dev_;
};

#endif //_MAGMA_SYSTEM_DEVICE_H_