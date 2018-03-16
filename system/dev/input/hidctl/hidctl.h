// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddk/device.h>
#include <ddktl/device.h>
#include <ddktl/protocol/hidbus.h>
#include <fbl/mutex.h>
#include <fbl/unique_ptr.h>
#include <threads.h>
#include <zircon/compiler.h>
#include <zircon/device/hidctl.h>
#include <zircon/types.h>
#include <lib/zx/socket.h>

namespace hidctl {

class HidCtl : public ddk::Device<HidCtl, ddk::Ioctlable> {
  public:
    HidCtl(zx_device_t* device);

    void DdkRelease();
    zx_status_t DdkIoctl(uint32_t op, const void* in_buf, size_t in_len, void* out_buf,
                         size_t out_len, size_t* out_actual);
};

class HidDevice : public ddk::Device<HidDevice, ddk::Unbindable>,
                  public ddk::HidBusProtocol<HidDevice> {
  public:
    HidDevice(zx_device_t* device, const hid_ioctl_config* config, zx::socket data);

    void DdkRelease();
    void DdkUnbind();

    zx_status_t HidBusQuery(uint32_t options, hid_info_t* info);
    zx_status_t HidBusStart(ddk::HidBusIfcProxy proxy);
    void HidBusStop();
    zx_status_t HidBusGetDescriptor(uint8_t desc_type, void** data, size_t* len);
    zx_status_t HidBusGetReport(uint8_t rpt_type, uint8_t rpt_id, void* data, size_t len,
                                size_t* out_len);
    zx_status_t HidBusSetReport(uint8_t rpt_type, uint8_t rpt_id, void* data, size_t len);
    zx_status_t HidBusGetIdle(uint8_t rpt_id, uint8_t* duration);
    zx_status_t HidBusSetIdle(uint8_t rpt_id, uint8_t duration);
    zx_status_t HidBusGetProtocol(uint8_t* protocol);
    zx_status_t HidBusSetProtocol(uint8_t protocol);

    int Thread();
    void Shutdown();

  private:
    zx_status_t Recv(uint8_t* buffer, uint32_t capacity);

    bool boot_device_;
    uint8_t dev_class_;
    fbl::unique_ptr<uint8_t[]> report_desc_;
    size_t report_desc_len_ = 0;
    uint32_t mtu_ = 256;  // TODO: set this based on report_desc_

    fbl::Mutex lock_;
    ddk::HidBusIfcProxy proxy_ __TA_GUARDED(lock_);
    zx::socket data_;
    thrd_t thread_;
};

}  // namespace hidctl
