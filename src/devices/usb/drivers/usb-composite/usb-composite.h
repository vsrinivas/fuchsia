// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_USB_DRIVERS_USB_COMPOSITE_USB_COMPOSITE_H_
#define SRC_DEVICES_USB_DRIVERS_USB_COMPOSITE_USB_COMPOSITE_H_

#include <fuchsia/hardware/usb/cpp/banjo.h>
#include <zircon/hw/usb.h>

#include <ddktl/device.h>
#include <fbl/array.h>
#include <fbl/mutex.h>
#include <fbl/ref_ptr.h>
#include <fbl/vector.h>

namespace usb_composite {

class UsbComposite;
class UsbInterface;
using UsbCompositeType = ddk::Device<UsbComposite, ddk::Unbindable>;

// This class represents a USB composite device and manages creating devmgr devices
// for multiple USB interfaces.
class UsbComposite : public UsbCompositeType {
 public:
  UsbComposite(zx_device_t* parent) : UsbCompositeType(parent), usb_(parent) {}

  static zx_status_t Create(void* ctx, zx_device_t* parent);

  // Device protocol implementation.
  void DdkUnbind(ddk::UnbindTxn txn);
  void DdkRelease();

  zx_status_t ClaimInterface(uint8_t interface_id);
  zx_status_t SetInterface(uint8_t interface_id, uint8_t alt_setting);
  zx_status_t GetAdditionalDescriptorList(uint8_t last_interface_id, uint8_t* out_desc_list,
                                          size_t desc_count, size_t* out_desc_actual);

  inline const usb_device_descriptor_t* device_descriptor() const { return &device_desc_; }
  inline const usb_configuration_descriptor_t* GetConfigurationDescriptor() const {
    return reinterpret_cast<usb_configuration_descriptor_t*>(config_desc_.data());
  }

 private:
  enum class InterfaceStatus : uint8_t {
    // The interface has not been claimed and no device has been created for it.
    AVAILABLE,
    // Another interface has claimed the interface.
    CLAIMED,
    // A child device has been created for the interface.
    CHILD_DEVICE,
  };

  zx_status_t Init();

  zx_status_t AddInterface(const usb_interface_descriptor_t* interface_desc, size_t length);
  zx_status_t AddInterfaceAssoc(const usb_interface_assoc_descriptor_t* assoc_desc, size_t length);
  zx_status_t AddInterfaces();
  fbl::RefPtr<UsbInterface> GetInterfaceById(uint8_t interface_id) __TA_REQUIRES(lock_);
  bool RemoveInterfaceById(uint8_t interface_id) __TA_REQUIRES(lock_);

  // Our parent's USB protocol.
  const ddk::UsbProtocolClient usb_;
  // Array of all our USB interfaces.
  fbl::Vector<fbl::RefPtr<UsbInterface>> interfaces_ __TA_GUARDED(lock_);

  InterfaceStatus interface_statuses_[UINT8_MAX] __TA_GUARDED(lock_) = {};

  usb_device_descriptor_t device_desc_;
  fbl::Array<uint8_t> config_desc_;

  fbl::Mutex lock_;
};

}  // namespace usb_composite

#endif  // SRC_DEVICES_USB_DRIVERS_USB_COMPOSITE_USB_COMPOSITE_H_
