// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_INPUT_DRIVERS_USB_HID_FUNCTION_TWO_ENDPOINT_HID_FUNCTION_H_
#define SRC_UI_INPUT_DRIVERS_USB_HID_FUNCTION_TWO_ENDPOINT_HID_FUNCTION_H_

#include <lib/zircon-internal/thread_annotations.h>
#include <zircon/hw/usb/hid.h>

#include <memory>
#include <optional>
#include <vector>

#include <ddk/device.h>
#include <ddk/protocol/hidbus.h>
#include <ddktl/device.h>
#include <ddktl/protocol/usb/function.h>
#include <fbl/condition_variable.h>
#include <usb/request-cpp.h>
#include <usb/usb.h>

namespace two_endpoint_hid_function {

// This driver is for testing the USB-HID driver. It binds as a peripheral USB
// device and sends fake HID report descriptors and HID reports. The tests for
// this driver and the USB-HID driver are with the other usb-virtual-bus tests.
class FakeUsbHidFunction;
using DeviceType = ddk::Device<FakeUsbHidFunction, ddk::Unbindable>;
class FakeUsbHidFunction : public DeviceType {
 public:
  FakeUsbHidFunction(zx_device_t* parent) : DeviceType(parent), function_(parent) {}
  zx_status_t Bind();
  // |ddk::Device|
  void DdkUnbind(ddk::UnbindTxn txn);
  // |ddk::Device|
  void DdkRelease();

  void UsbEndpointOutCallback(usb_request_t* request);

  static size_t UsbFunctionInterfaceGetDescriptorsSize(void* ctx);

  static void UsbFunctionInterfaceGetDescriptors(void* ctx, void* out_descriptors_buffer,
                                                 size_t descriptors_size,
                                                 size_t* out_descriptors_actual);
  static zx_status_t UsbFunctionInterfaceControl(void* ctx, const usb_setup_t* setup,
                                                 const void* write_buffer, size_t write_size,
                                                 void* out_read_buffer, size_t read_size,
                                                 size_t* out_read_actual);
  static zx_status_t UsbFunctionInterfaceSetConfigured(void* ctx, bool configured,
                                                       usb_speed_t speed);
  static zx_status_t UsbFunctionInterfaceSetInterface(void* ctx, uint8_t interface,
                                                      uint8_t alt_setting);

 private:
  int Thread();

  usb_function_interface_protocol_ops_t function_interface_ops_{
      .get_descriptors_size = UsbFunctionInterfaceGetDescriptorsSize,
      .get_descriptors = UsbFunctionInterfaceGetDescriptors,
      .control = UsbFunctionInterfaceControl,
      .set_configured = UsbFunctionInterfaceSetConfigured,
      .set_interface = UsbFunctionInterfaceSetInterface,
  };
  ddk::UsbFunctionProtocolClient function_;

  std::vector<uint8_t> report_desc_;
  std::vector<uint8_t> report_ TA_GUARDED(mtx_);

  struct fake_usb_hid_descriptor_t {
    usb_interface_descriptor_t interface;
    usb_endpoint_descriptor_t interrupt_in;
    usb_endpoint_descriptor_t interrupt_out;
    usb_hid_descriptor_t hid_descriptor;
  } __PACKED;

  struct DescriptorDeleter {
    void operator()(fake_usb_hid_descriptor_t* desc) { free(desc); }
  };
  std::unique_ptr<fake_usb_hid_descriptor_t, DescriptorDeleter> descriptor_;
  size_t descriptor_size_;

  uint8_t hid_protocol_ = HID_PROTOCOL_REPORT;

  std::optional<usb::Request<>> data_out_req_;
  bool data_out_req_complete_ TA_GUARDED(mtx_) = true;
  bool active_ = false;

  fbl::ConditionVariable event_ TA_GUARDED(mtx_);
  thrd_t thread_ = {};
  fbl::Mutex mtx_;
};

}  // namespace two_endpoint_hid_function

#endif  // SRC_UI_INPUT_DRIVERS_USB_HID_FUNCTION_TWO_ENDPOINT_HID_FUNCTION_H_
