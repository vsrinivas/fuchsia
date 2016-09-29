// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef _MAGMA_SYSTEM_DEVICE_H_
#define _MAGMA_SYSTEM_DEVICE_H_

#include "magma_system_connection.h"
#include "magma_system_display.h"
#include "msd.h"
#include <functional>
#include <memory>

using msd_device_unique_ptr_t = std::unique_ptr<msd_device, decltype(&msd_device_destroy)>;

static inline msd_device_unique_ptr_t MsdDeviceUniquePtr(msd_device* msd_dev)
{
    return msd_device_unique_ptr_t(msd_dev, &msd_device_destroy);
}

class MagmaSystemDevice : public MagmaSystemConnection::Owner, public MagmaSystemDisplay::Owner {
public:
    MagmaSystemDevice(msd_device_unique_ptr_t msd_dev)
        : msd_dev_(std::move(msd_dev)), display_(new MagmaSystemDisplay(this))
    {
    }

    // Opens a connection to the device. This transfers ownership of this object to the
    // caller for now, since that is the semantics of what will happen when connections are message
    // pipes and the caller is another process. There will probably need to be a close function at
    // that point but we dont need it now so I havent included it
    // Close this connection by deleting the returned object
    // returns nullptr on failure
    std::unique_ptr<MagmaSystemConnection> Open(msd_client_id client_id);

    // Gets the display interface for this device. This will only return a valid pointer once
    std::unique_ptr<MagmaSystemDisplay> OpenDisplay() { return std::move(display_); }

    msd_device* msd_dev() { return msd_dev_.get(); }

    // MagmaSystemDisplay::Owner
    uint32_t GetTokenForBuffer(std::shared_ptr<MagmaSystemBuffer>) override;
    // Returns the device id. 0 is invalid.
    uint32_t GetDeviceId() override;

    // MagmaSystemDisplay::Owner
    std::shared_ptr<MagmaSystemBuffer> GetBufferForToken(uint32_t token) override;
    void PageFlip(std::shared_ptr<MagmaSystemBuffer> buf, magma_system_pageflip_callback_t callback,
                  void* data) override;

private:
    msd_device_unique_ptr_t msd_dev_;
    std::map<uint32_t, std::shared_ptr<MagmaSystemBuffer>> token_map_;
    uint32_t next_token_{};
    std::unique_ptr<MagmaSystemDisplay> display_;
};

#endif //_MAGMA_SYSTEM_DEVICE_H_