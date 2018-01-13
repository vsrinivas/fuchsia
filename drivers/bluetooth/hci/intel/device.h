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

class Device;

using DeviceType =
    ddk::Device<Device, ddk::GetProtocolable, ddk::Unbindable, ddk::Ioctlable>;

class Device : public DeviceType, public ddk::BtHciProtocol<Device> {
 public:
  Device(zx_device_t* device, bt_hci_protocol_t* hci);

  ~Device() = default;

  // Load the firmware and add the device.
  // If |secure| is true, use the "secure" firmware method.
  zx_status_t Bind(bool secure);

  // ddk::Device methods
  void DdkUnbind();
  void DdkRelease();
  zx_status_t DdkGetProtocol(uint32_t proto_id, void* out_proto);
  zx_status_t DdkIoctl(uint32_t op,
                       const void* in_buf,
                       size_t in_len,
                       void* out_buf,
                       size_t out_len,
                       size_t* actual);

 private:
  // Adds the device (or makes it visible if invisible)
  // |success_note| is appended to a kernel log message for success
  zx_status_t AddDevice(const char* success_note);

  // Maps the firmware refrenced by |name| into memory.
  // Returns the vmo that the firmware is loaded into or ZX_HANDLE_INVALID if it
  // could not be loaded.
  // Closing this handle will invalidate |fw_addr|, which
  // receives a pointer to the memory.
  // |fw_size| receives the size of the firmware if valid.
  zx_handle_t MapFirmware(const char* name,
                          uintptr_t* fw_addr,
                          size_t* fw_size);

  bt_hci_protocol_t hci_;
  bool firmware_loaded_;
};

}  // namespace btintel
