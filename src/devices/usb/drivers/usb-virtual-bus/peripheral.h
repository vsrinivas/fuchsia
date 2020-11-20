// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_USB_DRIVERS_USB_VIRTUAL_BUS_PERIPHERAL_H_
#define SRC_DEVICES_USB_DRIVERS_USB_VIRTUAL_BUS_PERIPHERAL_H_

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/metadata.h>
#include <ddk/platform-defs.h>
#include <ddktl/device.h>
#include <ddktl/protocol/usb/function.h>
#include <fbl/algorithm.h>
#include <fbl/condition_variable.h>
#include <fbl/mutex.h>
#include <usb/request-cpp.h>
#include <usb/usb-request.h>

namespace virtualbus {
constexpr auto kMaxPacketSize = 20;

class TestFunction;
using DeviceType = ddk::Device<TestFunction, ddk::Unbindable>;
class TestFunction : public DeviceType, public ddk::UsbFunctionInterfaceProtocol<TestFunction> {
 public:
  TestFunction(zx_device_t* parent) : DeviceType(parent), function_(parent) {}
  zx_status_t Bind();
  // |ddk::Device|
  void DdkUnbind(ddk::UnbindTxn txn);
  // |ddk::Device|
  void DdkRelease();

  size_t UsbFunctionInterfaceGetDescriptorsSize();

  void UsbFunctionInterfaceGetDescriptors(void* out_descriptors_buffer, size_t descriptors_size,
                                          size_t* out_descriptors_actual);
  zx_status_t UsbFunctionInterfaceControl(const usb_setup_t* setup, const void* write_buffer,
                                          size_t write_size, void* out_read_buffer,
                                          size_t read_size, size_t* out_read_actual);
  zx_status_t UsbFunctionInterfaceSetConfigured(bool configured, usb_speed_t speed);
  zx_status_t UsbFunctionInterfaceSetInterface(uint8_t interface, uint8_t alt_setting);

 private:
  void CompletionCallback(usb_request_t* req);

  ddk::UsbFunctionProtocolClient function_;

  struct VirtualBusTestDescriptor {
    usb_interface_descriptor_t interface;
    usb_endpoint_descriptor_t bulk_out;
  } __PACKED descriptor_;

  size_t descriptor_size_ = 0;

  size_t parent_req_size_ = 0;
  uint8_t bulk_out_addr_ = 0;

  bool configured_ = false;
  bool active_ = false;
};
}  // namespace virtualbus

#endif  // SRC_DEVICES_USB_DRIVERS_USB_VIRTUAL_BUS_PERIPHERAL_H_
