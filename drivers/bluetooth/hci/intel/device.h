// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddk/driver.h>
#include <ddk/protocol/bt-hci.h>
#include <ddktl/device.h>
#include <ddktl/protocol/bt-hci.h>

#include "vendor_hci.h"

namespace btintel {

class Device : public ddk::Device<Device, ddk::Unbindable, ddk::Ioctlable>,
               public ddk::BtHciProtocol<Device> {
 public:
  Device(zx_device_t* device, bt_hci_protocol_t* hci);

  ~Device() = default;

  zx_status_t Bind();

  // ddk::Device methods
  void DdkUnbind();
  void DdkRelease();

  zx_status_t DdkIoctl(uint32_t op,
                       const void* in_buf,
                       size_t in_len,
                       void* out_buf,
                       size_t out_len,
                       size_t* actual);

 private:
  bt_hci_protocol_t* hci_;
  bool firmware_loaded_;
};

}  // namespace btintel
