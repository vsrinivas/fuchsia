// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

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

    msd_device* msd_dev() { return msd_dev_.get(); }

    // Returns the device id. 0 is invalid.
    uint32_t GetDeviceId();

private:
    msd_device_unique_ptr_t msd_dev_;
};

#endif //_MAGMA_SYSTEM_DEVICE_H_