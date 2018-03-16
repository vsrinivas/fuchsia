// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddk/device.h>
#include <ddk/protocol/test.h>
#include <ddktl/device.h>
#include <ddktl/protocol/ethernet.h>
#include <ddktl/protocol/test.h>
#include <zircon/compiler.h>
#include <zircon/types.h>
#include <zircon/device/ethertap.h>
#include <lib/zx/socket.h>
#include <fbl/mutex.h>
#include <fbl/unique_ptr.h>
#include <threads.h>

namespace eth {

class TapCtl : public ddk::Device<TapCtl, ddk::Ioctlable> {
  public:
    TapCtl(zx_device_t* device);

    void DdkRelease();
    zx_status_t DdkIoctl(uint32_t op, const void* in_buf, size_t in_len, void* out_buf,
                         size_t out_len, size_t* out_actual);
};

class TapDevice : public ddk::Device<TapDevice, ddk::Unbindable>,
                  public ddk::EthmacProtocol<TapDevice> {
  public:
    TapDevice(zx_device_t* device, const ethertap_ioctl_config* config, zx::socket data);

    void DdkRelease();
    void DdkUnbind();

    zx_status_t EthmacQuery(uint32_t options, ethmac_info_t* info);
    void EthmacStop();
    zx_status_t EthmacStart(fbl::unique_ptr<ddk::EthmacIfcProxy> proxy);
    zx_status_t EthmacQueueTx(uint32_t options, ethmac_netbuf_t* netbuf);
    zx_status_t EthmacSetParam(uint32_t param, int32_t value, void* data);
    // No DMA capability, so return invalid handle for get_bti
    zx_handle_t EthmacGetBti();
    int Thread();

  private:
    zx_status_t UpdateLinkStatus(zx_signals_t observed);
    zx_status_t Recv(uint8_t* buffer, uint32_t capacity);

    // ethertap options
    uint32_t options_ = 0;

    // ethermac fields
    uint32_t features_ = 0;
    uint32_t mtu_ = 0;
    uint8_t mac_[6] = {};

    fbl::Mutex lock_;
    bool dead_ = false;
    fbl::unique_ptr<ddk::EthmacIfcProxy> ethmac_proxy_ __TA_GUARDED(lock_);

    // Only accessed from Thread, so not locked.
    bool online_ = false;
    zx::socket data_;

    thrd_t thread_;
};

}  // namespace eth
