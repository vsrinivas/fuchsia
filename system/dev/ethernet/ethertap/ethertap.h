// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddk/device.h>
#include <ddk/protocol/test.h>
#include <ddktl/device.h>
#include <ddktl/protocol/ethernet.h>
#include <ddktl/protocol/test.h>
#include <magenta/compiler.h>
#include <magenta/types.h>
#include <magenta/device/ethertap.h>
#include <mx/socket.h>
#include <fbl/mutex.h>
#include <fbl/unique_ptr.h>
#include <threads.h>

namespace eth {

class TapCtl : public ddk::Device<TapCtl, ddk::Ioctlable> {
  public:
    TapCtl(mx_device_t* device);

    void DdkRelease();
    mx_status_t DdkIoctl(uint32_t op, const void* in_buf, size_t in_len, void* out_buf,
                         size_t out_len, size_t* out_actual);
};

class TapDevice : public ddk::Device<TapDevice, ddk::Unbindable>,
                  public ddk::EthmacProtocol<TapDevice> {
  public:
    TapDevice(mx_device_t* device, const ethertap_ioctl_config* config, mx::socket data);

    void DdkRelease();
    void DdkUnbind();

    mx_status_t EthmacQuery(uint32_t options, ethmac_info_t* info);
    void EthmacStop();
    mx_status_t EthmacStart(fbl::unique_ptr<ddk::EthmacIfcProxy> proxy);
    void EthmacSend(uint32_t options, void* data, size_t length);

    int Thread();

  private:
    mx_status_t UpdateLinkStatus(mx_signals_t observed);
    mx_status_t Recv(uint8_t* buffer, uint32_t capacity);

    // ethertap options
    uint32_t options_ = 0;

    // ethermac fields
    uint32_t features_ = 0;
    uint32_t mtu_ = 0;
    uint8_t mac_[6] = {};

    fbl::Mutex lock_;
    fbl::unique_ptr<ddk::EthmacIfcProxy> ethmac_proxy_ __TA_GUARDED(lock_);

    // Only accessed from Thread, so not locked.
    bool online_ = false;
    mx::socket data_;

    thrd_t thread_;
};

}  // namespace eth
