// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_USB_DRIVERS_USB_COMPOSITE_USB_INTERFACE_H_
#define SRC_DEVICES_USB_DRIVERS_USB_COMPOSITE_USB_INTERFACE_H_

#include <fuchsia/hardware/usb/composite/cpp/banjo.h>
#include <fuchsia/hardware/usb/cpp/banjo.h>
#include <zircon/hw/usb.h>

#include <ddktl/device.h>
#include <fbl/array.h>
#include <fbl/mutex.h>
#include <fbl/ref_counted.h>
#include <fbl/ref_ptr.h>

#include "usb-composite.h"

namespace usb_composite {

class UsbComposite;
class UsbInterface;
using UsbInterfaceType = ddk::Device<UsbInterface, ddk::GetProtocolable>;

// This class represents a USB interface in a composite device.
class UsbInterface : public UsbInterfaceType,
                     public ddk::UsbProtocol<UsbInterface, ddk::base_protocol>,
                     public ddk::UsbCompositeProtocol<UsbInterface>,
                     public fbl::RefCounted<UsbInterface> {
 public:
  UsbInterface(zx_device_t* parent, UsbComposite* composite, const ddk::UsbProtocolClient& usb)
      : UsbInterfaceType(parent), composite_(composite), usb_(usb) {}

  static zx_status_t Create(zx_device_t* parent, UsbComposite* composite,
                            const ddk::UsbProtocolClient& usb,
                            const usb_interface_descriptor_t* interface_desc, size_t desc_length,
                            fbl::RefPtr<UsbInterface>* out_interface);
  static zx_status_t Create(zx_device_t* parent, UsbComposite* composite,
                            const ddk::UsbProtocolClient& usb,
                            const usb_interface_assoc_descriptor_t* assoc_desc, size_t desc_length,
                            fbl::RefPtr<UsbInterface>* out_interface);

  // Device protocol implementation.
  zx_status_t DdkGetProtocol(uint32_t proto_id, void* out);
  void DdkRelease();

  // USB protocol implementation.
  zx_status_t UsbControlOut(uint8_t request_type, uint8_t request, uint16_t value, uint16_t index,
                            int64_t timeout, const uint8_t* write_buffer, size_t write_size);
  zx_status_t UsbControlIn(uint8_t request_type, uint8_t request, uint16_t value, uint16_t index,
                           int64_t timeout, uint8_t* out_read_buffer, size_t read_size,
                           size_t* out_read_actual);
  void UsbRequestQueue(usb_request_t* usb_request,
                       const usb_request_complete_callback_t* complete_cb);
  usb_speed_t UsbGetSpeed();
  zx_status_t UsbSetInterface(uint8_t interface_number, uint8_t alt_setting);
  uint8_t UsbGetConfiguration();
  zx_status_t UsbSetConfiguration(uint8_t configuration);
  zx_status_t UsbEnableEndpoint(const usb_endpoint_descriptor_t* ep_desc,
                                const usb_ss_ep_comp_descriptor_t* ss_com_desc, bool enable);
  zx_status_t UsbResetEndpoint(uint8_t ep_address);
  zx_status_t UsbResetDevice();
  size_t UsbGetMaxTransferSize(uint8_t ep_address);
  uint32_t UsbGetDeviceId();
  void UsbGetDeviceDescriptor(usb_device_descriptor_t* out_desc);
  zx_status_t UsbGetConfigurationDescriptorLength(uint8_t configuration, size_t* out_length);
  zx_status_t UsbGetConfigurationDescriptor(uint8_t configuration, uint8_t* out_desc_buffer,
                                            size_t desc_size, size_t* out_desc_actual);
  size_t UsbGetDescriptorsLength();
  void UsbGetDescriptors(uint8_t* out_descs_buffer, size_t descs_size, size_t* out_descs_actual);
  zx_status_t UsbGetStringDescriptor(uint8_t desc_id, uint16_t lang_id, uint16_t* out_lang_id,
                                     uint8_t* out_string_buffer, size_t string_size,
                                     size_t* out_string_actual);
  zx_status_t UsbCancelAll(uint8_t ep_address);
  uint64_t UsbGetCurrentFrame();
  size_t UsbGetRequestSize();

  // USB composite protocol implementation.
  size_t UsbCompositeGetAdditionalDescriptorLength();
  zx_status_t UsbCompositeGetAdditionalDescriptorList(uint8_t* out_desc_list, size_t desc_count,
                                                      size_t* out_desc_actual);
  zx_status_t UsbCompositeClaimInterface(const usb_interface_descriptor_t* desc, uint32_t length);

  // FIDL messages.
  zx_status_t MsgGetDeviceSpeed(fidl_txn_t* txn);
  zx_status_t MsgGetDeviceDescriptor(fidl_txn_t* txn);
  zx_status_t MsgGetConfigurationDescriptorSize(uint8_t config, fidl_txn_t* txn);
  zx_status_t MsgGetConfigurationDescriptor(uint8_t config, fidl_txn_t* txn);
  zx_status_t MsgGetStringDescriptor(uint8_t desc_id, uint16_t lang_id, fidl_txn_t* txn);
  zx_status_t MsgSetInterface(uint8_t interface_number, uint8_t alt_setting, fidl_txn_t* txn);
  zx_status_t MsgGetDeviceId(fidl_txn_t* txn);
  zx_status_t MsgGetHubDeviceId(fidl_txn_t* txn);
  zx_status_t MsgGetConfiguration(fidl_txn_t* txn);
  zx_status_t MsgSetConfiguration(uint8_t configuration, fidl_txn_t* txn);

  bool ContainsInterface(uint8_t interface_id);
  zx_status_t SetAltSetting(uint8_t interface_id, uint8_t alt_setting);

  inline uint8_t usb_class() const { return usb_class_; }
  inline uint8_t usb_subclass() const { return usb_subclass_; }
  inline uint8_t usb_protocol() const { return usb_protocol_; }

 private:
  zx_status_t Init(const void* descriptors, size_t desc_length, uint8_t last_interface_id,
                   uint8_t usb_class, uint8_t usb_subclass, uint8_t usb_protocol);
  zx_status_t ConfigureEndpoints(uint8_t interface_id, uint8_t alt_setting);

  UsbComposite* composite_;
  const ddk::UsbProtocolClient usb_;

  uint8_t usb_class_;
  uint8_t usb_subclass_;
  uint8_t usb_protocol_;

  // ID of the last interface in the descriptor list.
  uint8_t last_interface_id_;

  fbl::Array<uint8_t> descriptors_;

  // Descriptors for currently active endpoints.
  // These point into descriptor_'s storage.
  const usb_endpoint_descriptor_t* active_endpoints_[USB_MAX_EPS] = {};
};

}  // namespace usb_composite

#endif  // SRC_DEVICES_USB_DRIVERS_USB_COMPOSITE_USB_INTERFACE_H_
