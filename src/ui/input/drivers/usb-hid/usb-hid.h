// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_INPUT_DRIVERS_USB_HID_USB_HID_H_
#define SRC_UI_INPUT_DRIVERS_USB_HID_USB_HID_H_

#include <lib/sync/completion.h>
#include <zircon/hw/usb/hid.h>

#include <memory>
#include <thread>

#include <ddktl/device.h>
#include <ddktl/protocol/hidbus.h>
#include <ddktl/protocol/usb.h>
#include <fbl/condition_variable.h>
#include <fbl/mutex.h>
#include <usb/usb.h>

namespace usb_hid {

class UsbHidbus;
using DeviceType = ddk::Device<UsbHidbus, ddk::Unbindable>;

class UsbHidbus : public DeviceType, public ddk::HidbusProtocol<UsbHidbus, ddk::base_protocol> {
 public:
  explicit UsbHidbus(zx_device_t* device) : DeviceType(device) {}

  // Methods required by the ddk mixins.
  void UsbInterruptCallback(usb_request_t* req);
  zx_status_t HidbusQuery(uint32_t options, hid_info_t* info);
  zx_status_t HidbusStart(const hidbus_ifc_protocol_t* ifc);
  void HidbusStop();
  zx_status_t UsbHidControl(uint8_t req_type, uint8_t request, uint16_t value, uint16_t index,
                            void* data, size_t length, size_t* out_length);
  zx_status_t UsbHidControlIn(uint8_t req_type, uint8_t request, uint16_t value, uint16_t index,
                              void* data, size_t length, size_t* out_length);
  zx_status_t UsbHidControlOut(uint8_t req_type, uint8_t request, uint16_t value, uint16_t index,
                               const void* data, size_t length, size_t* out_length);
  zx_status_t HidbusGetDescriptor(hid_description_type_t desc_type, void* out_data_buffer,
                                  size_t data_size, size_t* out_data_actual);
  zx_status_t HidbusGetReport(uint8_t rpt_type, uint8_t rpt_id, void* data, size_t len,
                              size_t* out_len);
  zx_status_t HidbusSetReport(uint8_t rpt_type, uint8_t rpt_id, const void* data, size_t len);
  zx_status_t HidbusGetIdle(uint8_t rpt_id, uint8_t* duration);
  zx_status_t HidbusSetIdle(uint8_t rpt_id, uint8_t duration);
  zx_status_t HidbusGetProtocol(uint8_t* protocol);
  zx_status_t HidbusSetProtocol(uint8_t protocol);

  void DdkUnbind(ddk::UnbindTxn txn);
  void UsbHidRelease();
  void DdkRelease();
  void FindDescriptors(usb::Interface interface, usb_hid_descriptor_t** hid_desc,
                       const usb_endpoint_descriptor_t** endptin,
                       const usb_endpoint_descriptor_t** endptout);
  zx_status_t Bind(ddk::UsbProtocolClient usbhid);

 private:
  std::optional<usb::InterfaceList> usb_interface_list_;

  // These pointers are valid as long as usb_interface_list_ is valid.
  usb_hid_descriptor_t* hid_desc_ = nullptr;

  uint8_t endptin_address_ = 0;
  uint8_t endptout_address_ = 0;
  // This boolean is set to true for a usb device that has an interrupt out endpoint. The interrupt
  // out endpoint is used to send reports to the device. (the SET report protocol).
  bool has_endptout_ = false;
  size_t endptout_max_size_ = 0;

  hid_info_t info_ = {};
  usb_request_t* req_ = nullptr;
  usb_request_t* request_out_ = nullptr;
  bool req_queued_ = false;

  ddk::UsbProtocolClient usb_ = {};

  fbl::Mutex hidbus_ifc_lock_;
  ddk::HidbusIfcProtocolClient ifc_ __TA_GUARDED(hidbus_ifc_lock_) = {};

  uint8_t interface_ = 0;
  usb_desc_iter_t desc_iter_ = {};
  size_t parent_req_size_ = 0;

  std::thread unbind_thread_;
  sync_completion_t set_report_complete_;
};

}  // namespace usb_hid

#endif  // SRC_UI_INPUT_DRIVERS_USB_HID_USB_HID_H_
