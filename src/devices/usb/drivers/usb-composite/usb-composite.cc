// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "usb-composite.h"

#include <stdio.h>

#include <fbl/auto_lock.h>

#include "usb-interface.h"

namespace usb_composite {

static inline const usb_descriptor_header_t* NextDescriptor(const void* header) {
  auto* desc = reinterpret_cast<const usb_descriptor_header_t*>(header);
  return reinterpret_cast<const usb_descriptor_header_t*>(reinterpret_cast<const uint8_t*>(desc) +
                                                          desc->bLength);
}

zx_status_t UsbComposite::Create(void* ctx, zx_device_t* parent) {
  fbl::AllocChecker ac;
  auto device = fbl::make_unique_checked<UsbComposite>(&ac, parent);
  if (!ac.check()) {
    return ZX_ERR_NO_MEMORY;
  }

  auto status = device->Init();
  if (status != ZX_OK) {
    return status;
  }

  // devmgr is now in charge of the device.
  __UNUSED auto* dummy = device.release();
  return ZX_OK;
}

// returns whether the interface with the given id was removed.
bool UsbComposite::RemoveInterfaceById(uint8_t interface_id) {
  size_t index = 0;
  for (auto intf : interfaces_) {
    if (intf->ContainsInterface(interface_id)) {
      intf->DdkRemoveDeprecated();
      interfaces_.erase(index);
      return true;
    }
    index++;
  }
  return false;
}

zx_status_t UsbComposite::AddInterface(const usb_interface_descriptor_t* interface_desc,
                                       size_t length) {
  fbl::RefPtr<UsbInterface> interface;
  auto status = UsbInterface::Create(zxdev(), this, usb_, interface_desc, length, &interface);
  if (status != ZX_OK) {
    return status;
  }

  {
    fbl::AutoLock lock(&lock_);
    // We need to do this first before calling DdkAdd().
    interfaces_.push_back(interface);
  }

  char name[20];
  snprintf(name, sizeof(name), "ifc-%03d", interface_desc->bInterfaceNumber);

  zx_device_prop_t props[] = {
      {BIND_PROTOCOL, 0, ZX_PROTOCOL_USB},
      {BIND_USB_VID, 0, device_desc_.idVendor},
      {BIND_USB_PID, 0, device_desc_.idProduct},
      {BIND_USB_CLASS, 0, interface->usb_class()},
      {BIND_USB_SUBCLASS, 0, interface->usb_subclass()},
      {BIND_USB_PROTOCOL, 0, interface->usb_protocol()},
  };

  status = interface->DdkAdd(ddk::DeviceAddArgs(name).set_props(props));
  if (status == ZX_OK) {
    // Hold a reference while devmgr has a pointer to this object.
    interface->AddRef();
  } else {
    fbl::AutoLock lock(&lock_);
    interfaces_.pop_back();
  }

  return status;
}

zx_status_t UsbComposite::AddInterfaceAssoc(const usb_interface_assoc_descriptor_t* assoc_desc,
                                            size_t length) {
  fbl::RefPtr<UsbInterface> interface;
  auto status = UsbInterface::Create(zxdev(), this, usb_, assoc_desc, length, &interface);
  if (status != ZX_OK) {
    return status;
  }

  {
    fbl::AutoLock lock(&lock_);
    // We need to do this first before calling DdkAdd().
    interfaces_.push_back(interface);
  }

  char name[20];
  snprintf(name, sizeof(name), "asc-%03d", assoc_desc->iFunction);

  zx_device_prop_t props[] = {
      {BIND_PROTOCOL, 0, ZX_PROTOCOL_USB},
      {BIND_USB_VID, 0, device_desc_.idVendor},
      {BIND_USB_PID, 0, device_desc_.idProduct},
      {BIND_USB_CLASS, 0, interface->usb_class()},
      {BIND_USB_SUBCLASS, 0, interface->usb_subclass()},
      {BIND_USB_PROTOCOL, 0, interface->usb_protocol()},
  };

  status = interface->DdkAdd(ddk::DeviceAddArgs(name).set_props(props));
  if (status == ZX_OK) {
    // Hold a reference while devmgr has a pointer to this object.
    interface->AddRef();
  } else {
    fbl::AutoLock lock(&lock_);
    interfaces_.pop_back();
  }

  return status;
}

zx_status_t UsbComposite::AddInterfaces() {
  auto* header = reinterpret_cast<const usb_descriptor_header_t*>(config_desc_.data());
  auto* config = reinterpret_cast<const usb_configuration_descriptor_t*>(header);
  auto* end = reinterpret_cast<const usb_descriptor_header_t*>(config_desc_.data() +
                                                               le16toh(config->wTotalLength));
  header = NextDescriptor(header);

  zx_status_t result = ZX_OK;

  while (header < end) {
    if (header->bDescriptorType == USB_DT_INTERFACE_ASSOCIATION) {
      auto* assoc_desc = reinterpret_cast<const usb_interface_assoc_descriptor_t*>(header);
      int interface_count = assoc_desc->bInterfaceCount;

      // find end of this interface association
      auto* next = NextDescriptor(assoc_desc);
      while (next < end) {
        if (next->bDescriptorType == USB_DT_INTERFACE_ASSOCIATION) {
          break;
        } else if (next->bDescriptorType == USB_DT_INTERFACE) {
          auto* test_intf = reinterpret_cast<const usb_interface_descriptor_t*>(next);

          if (test_intf->bAlternateSetting == 0) {
            if (interface_count == 0) {
              break;
            }
            interface_count--;
          }
        }
        next = NextDescriptor(next);
      }

      size_t length = reinterpret_cast<uintptr_t>(next) - reinterpret_cast<uintptr_t>(assoc_desc);
      auto status = AddInterfaceAssoc(assoc_desc, length);
      if (status != ZX_OK) {
        result = status;
      }

      header = next;
    } else if (header->bDescriptorType == USB_DT_INTERFACE) {
      auto* intf_desc = reinterpret_cast<const usb_interface_descriptor_t*>(header);
      // find end of current interface descriptor
      auto* next = NextDescriptor(intf_desc);
      while (next < end) {
        if (next->bDescriptorType == USB_DT_INTERFACE) {
          auto* test_intf = reinterpret_cast<const usb_interface_descriptor_t*>(next);
          // Iterate until we find the next top-level interface
          // Include alternate interfaces in the current interface
          if (test_intf->bAlternateSetting == 0) {
            break;
          }
        }
        next = NextDescriptor(next);
      }

      auto intf_num = intf_desc->bInterfaceNumber;
      InterfaceStatus intf_status;
      {
        fbl::AutoLock lock(&lock_);

        // Only create a child device if no child interface has claimed this interface.
        intf_status = interface_statuses_[intf_num];
      }

      size_t length = reinterpret_cast<uintptr_t>(next) - reinterpret_cast<uintptr_t>(intf_desc);
      if (intf_status == InterfaceStatus::AVAILABLE) {
        auto status = AddInterface(intf_desc, length);
        if (status != ZX_OK) {
          result = status;
        }

        fbl::AutoLock lock(&lock_);

        // The interface may have been claimed in the meanwhile, so we need to
        // check the interface status again.
        if (interface_statuses_[intf_num] == InterfaceStatus::CLAIMED) {
          bool removed = RemoveInterfaceById(intf_num);
          if (!removed) {
            return ZX_ERR_BAD_STATE;
          }
        } else {
          interface_statuses_[intf_num] = InterfaceStatus::CHILD_DEVICE;
        }
      }
      header = next;
    } else {
      header = NextDescriptor(header);
    }
  }

  return result;
}

fbl::RefPtr<UsbInterface> UsbComposite::GetInterfaceById(uint8_t interface_id) {
  for (auto intf : interfaces_) {
    if (intf->ContainsInterface(interface_id)) {
      return intf;
    }
  }
  return nullptr;
}

zx_status_t UsbComposite::ClaimInterface(uint8_t interface_id) {
  fbl::AutoLock lock(&lock_);

  auto intf = GetInterfaceById(interface_id);
  if (intf == nullptr) {
    return ZX_ERR_INVALID_ARGS;
  }
  if (interface_statuses_[interface_id] == InterfaceStatus::CLAIMED) {
    // The interface has already been claimed by a different interface.
    return ZX_ERR_ALREADY_BOUND;
  } else if (interface_statuses_[interface_id] == InterfaceStatus::CHILD_DEVICE) {
    bool removed = RemoveInterfaceById(interface_id);
    if (!removed) {
      return ZX_ERR_BAD_STATE;
    }
  }
  interface_statuses_[interface_id] = InterfaceStatus::CLAIMED;

  return ZX_OK;
}

zx_status_t UsbComposite::SetInterface(uint8_t interface_id, uint8_t alt_setting) {
  fbl::AutoLock lock(&lock_);

  for (auto intf : interfaces_) {
    if (intf->ContainsInterface(interface_id)) {
      return intf->SetAltSetting(interface_id, alt_setting);
    }
  }
  return ZX_ERR_INVALID_ARGS;
}

zx_status_t UsbComposite::GetAdditionalDescriptorList(uint8_t last_interface_id,
                                                      uint8_t* out_desc_list, size_t desc_count,
                                                      size_t* out_desc_actual) {
  *out_desc_actual = 0;

  auto* header = reinterpret_cast<const usb_descriptor_header_t*>(config_desc_.data());
  auto* config = reinterpret_cast<const usb_configuration_descriptor_t*>(header);
  auto* end = reinterpret_cast<const usb_descriptor_header_t*>(config_desc_.data() +
                                                               le16toh(config->wTotalLength));
  header = NextDescriptor(header);

  usb_interface_descriptor_t* result = NULL;
  while (header < end) {
    if (header->bDescriptorType == USB_DT_INTERFACE) {
      usb_interface_descriptor_t* test_intf = (usb_interface_descriptor_t*)header;
      // We are only interested in descriptors past the last stored descriptor
      // for the current interface.
      if (test_intf->bAlternateSetting == 0 && test_intf->bInterfaceNumber > last_interface_id) {
        result = test_intf;
        break;
      }
    }
    header = NextDescriptor(header);
  }
  if (!result) {
    return ZX_OK;
  }
  size_t length = reinterpret_cast<uintptr_t>(end) - reinterpret_cast<uintptr_t>(result);
  if (length > desc_count) {
    return ZX_ERR_BUFFER_TOO_SMALL;
  }
  memcpy(out_desc_list, result, length);
  *out_desc_actual = length;
  return ZX_OK;
}

void UsbComposite::DdkUnbindDeprecated() {
  {
    fbl::AutoLock lock(&lock_);
    for (auto interface : interfaces_) {
      interface->DdkRemoveDeprecated();
    }
    interfaces_.reset();
  }

  DdkRemoveDeprecated();
}

void UsbComposite::DdkRelease() { delete this; }

zx_status_t UsbComposite::Init() {
  // Parent must support USB protocol.
  if (!usb_.is_valid()) {
    return ZX_ERR_NOT_SUPPORTED;
  }

  usb_.GetDeviceDescriptor(&device_desc_);

  auto configuration = usb_.GetConfiguration();
  size_t desc_length;
  auto status = usb_.GetConfigurationDescriptorLength(configuration, &desc_length);
  if (status != ZX_OK) {
    return status;
  }

  fbl::AllocChecker ac;
  auto desc_bytes = new (&ac) uint8_t[desc_length];
  if (!ac.check()) {
    return ZX_ERR_NO_MEMORY;
  }
  config_desc_.reset(desc_bytes, desc_length);

  size_t actual;
  status = usb_.GetConfigurationDescriptor(configuration, desc_bytes, desc_length, &actual);
  if (status == ZX_OK && actual != desc_length) {
    status = ZX_ERR_IO;
  }
  if (status != ZX_OK) {
    return status;
  }

  char name[16];
  snprintf(name, sizeof(name), "%03d", usb_.GetDeviceId());

  status = DdkAdd(name, DEVICE_ADD_NON_BINDABLE);
  if (status != ZX_OK) {
    return status;
  }

  return AddInterfaces();
}

static constexpr zx_driver_ops_t driver_ops = []() {
  zx_driver_ops_t ops = {};
  ops.version = DRIVER_OPS_VERSION;
  ops.bind = UsbComposite::Create;
  return ops;
}();

}  // namespace usb_composite

// The '*' in the version string is important. This marks this driver as a fallback,
// to allow other drivers to bind against ZX_PROTOCOL_USB_DEVICE to handle more specific cases.
ZIRCON_DRIVER_BEGIN(usb_composite, usb_composite::driver_ops, "zircon", "*0.1", 1)
BI_MATCH_IF(EQ, BIND_PROTOCOL, ZX_PROTOCOL_USB_DEVICE), ZIRCON_DRIVER_END(usb_composite)
