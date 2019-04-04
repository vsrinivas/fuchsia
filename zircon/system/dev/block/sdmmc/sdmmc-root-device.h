// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <threads.h>

#include <atomic>

#include <ddktl/device.h>
#include <ddktl/protocol/sdmmc.h>
#include <fbl/ref_ptr.h>

#include "sdio-device.h"
#include "sdmmc-block-device.h"

namespace sdmmc {

class SdmmcRootDevice;
using SdmmcRootDeviceType = ddk::Device<SdmmcRootDevice, ddk::Unbindable>;

class SdmmcRootDevice : public SdmmcRootDeviceType {
public:
    static zx_status_t Bind(void* ctx, zx_device_t* parent);

    void DdkUnbind();
    void DdkRelease();

    zx_status_t Init();

private:
    SdmmcRootDevice(zx_device_t* parent, const ddk::SdmmcProtocolClient& host)
        : SdmmcRootDeviceType(parent), host_(host) {}

    int WorkerThread();

    const ddk::SdmmcProtocolClient host_;

    thrd_t worker_thread_ = 0;

    std::atomic<bool> dead_ = false;

    fbl::RefPtr<SdmmcBlockDevice> block_dev_;
    fbl::RefPtr<SdioDevice> sdio_dev_;
};

}  // namespace sdmmc
