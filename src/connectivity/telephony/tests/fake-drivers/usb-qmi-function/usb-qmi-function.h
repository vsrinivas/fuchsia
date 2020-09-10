// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_TELEPHONY_TESTS_FAKE_DRIVERS_USB_QMI_FUNCTION_USB_QMI_FUNCTION_H_
#define SRC_CONNECTIVITY_TELEPHONY_TESTS_FAKE_DRIVERS_USB_QMI_FUNCTION_USB_QMI_FUNCTION_H_

#include <memory>
#include <optional>
#include <vector>

#include <ddk/device.h>
#include <ddktl/device.h>
#include <ddktl/protocol/usb/function.h>

namespace usb_qmi_function {
constexpr size_t kUsbIntfDummySize = 8;

// This driver is for testing the USB-QMI driver. It binds as a peripheral USB
// device and sends fake QMI responses.
class FakeUsbQmiFunction : public ddk::Device<FakeUsbQmiFunction, ddk::Unbindable>,
                           public ddk::UsbFunctionInterfaceProtocol<FakeUsbQmiFunction> {
 public:
  FakeUsbQmiFunction(zx_device_t* parent)
      : ddk::Device<FakeUsbQmiFunction, ddk::Unbindable>(parent), function_(parent) {}
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
  void QmiCompletionCallback(usb_request_t* req);

 private:
  ddk::UsbFunctionProtocolClient function_;

  struct FakeUsbQmiDescriptor {
    usb_interface_descriptor_t interface_dummy[kUsbIntfDummySize];
    usb_interface_descriptor_t interface;  // real driver binds to intf number 8
    usb_endpoint_descriptor_t interrupt;
    usb_endpoint_descriptor_t bulk_in;
    usb_endpoint_descriptor_t bulk_out;
  } __PACKED;
  uint8_t tx_data_[256];
  size_t tx_size_;
  FakeUsbQmiDescriptor descriptor_;
  size_t descriptor_size_;
  std::optional<usb_request_t*> usb_int_req_;
  std::optional<usb_request_t*> usb_req_temp_;
};

}  // namespace usb_qmi_function

#endif  // SRC_CONNECTIVITY_TELEPHONY_TESTS_FAKE_DRIVERS_USB_QMI_FUNCTION_USB_QMI_FUNCTION_H_
