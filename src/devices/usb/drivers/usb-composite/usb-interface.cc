// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "usb-interface.h"

#include <fuchsia/hardware/usb/c/banjo.h>
#include <fuchsia/hardware/usb/composite/c/banjo.h>
#include <lib/ddk/debug.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <usb/usb-request.h>

#include "usb-composite.h"

namespace usb_composite {

zx_status_t UsbInterface::Create(zx_device_t* parent, UsbComposite* composite,
                                 const ddk::UsbProtocolClient& usb,
                                 const usb_interface_descriptor_t* interface_desc,
                                 size_t desc_length, fbl::RefPtr<UsbInterface>* out_interface) {
  fbl::AllocChecker ac;
  auto interface =
      fbl::MakeRefCountedChecked<UsbInterface>(&ac, composite->zxdev(), composite, usb);
  if (!ac.check()) {
    return ZX_ERR_NO_MEMORY;
  }

  auto* device_desc = composite->device_descriptor();
  uint8_t usb_class, usb_subclass, usb_protocol;

  if (interface_desc->b_interface_class == 0) {
    usb_class = device_desc->b_device_class;
    usb_subclass = device_desc->b_device_sub_class;
    usb_protocol = device_desc->b_device_protocol;
  } else {
    // class/subclass/protocol defined per-interface
    usb_class = interface_desc->b_interface_class;
    usb_subclass = interface_desc->b_interface_sub_class;
    usb_protocol = interface_desc->b_interface_protocol;
  }

  auto status = interface->Init(interface_desc, desc_length, interface_desc->b_interface_number,
                                usb_class, usb_subclass, usb_protocol);
  if (status != ZX_OK) {
    return status;
  }

  status = interface->ConfigureEndpoints(interface_desc->b_interface_number, 0);
  if (status != ZX_OK) {
    return status;
  }

  *out_interface = interface;
  return ZX_OK;
}

zx_status_t UsbInterface::Create(zx_device_t* parent, UsbComposite* composite,
                                 const ddk::UsbProtocolClient& usb,
                                 const usb_interface_assoc_descriptor_t* assoc_desc,
                                 size_t desc_length, fbl::RefPtr<UsbInterface>* out_interface) {
  fbl::AllocChecker ac;
  auto interface =
      fbl::MakeRefCountedChecked<UsbInterface>(&ac, composite->zxdev(), composite, usb);
  if (!ac.check()) {
    return ZX_ERR_NO_MEMORY;
  }

  auto* device_desc = composite->device_descriptor();
  uint8_t usb_class, usb_subclass, usb_protocol;

  if (assoc_desc->b_function_class == 0) {
    usb_class = device_desc->b_device_class;
    usb_subclass = device_desc->b_device_sub_class;
    usb_protocol = device_desc->b_device_protocol;
  } else {
    // class/subclass/protocol defined per-interface
    usb_class = assoc_desc->b_function_class;
    usb_subclass = assoc_desc->b_function_sub_class;
    usb_protocol = assoc_desc->b_function_protocol;
  }

  // Interfaces in an IAD interface collection must be contiguous.
  auto last_interface_id = assoc_desc->b_first_interface + assoc_desc->b_interface_count - 1;
  auto status = interface->Init(assoc_desc, desc_length, static_cast<uint8_t>(last_interface_id),
                                usb_class, usb_subclass, usb_protocol);
  if (status != ZX_OK) {
    return status;
  }

  usb_desc_iter_t header_iter;
  status = usb_desc_iter_init_unowned(const_cast<usb_interface_assoc_descriptor_t*>(assoc_desc),
                                      desc_length, &header_iter);
  if (status != ZX_OK) {
    zxlogf(ERROR, "Could not initialize iterator");
    return status;
  }

  usb_descriptor_header_t* header;
  while ((header = usb_desc_iter_peek(&header_iter)) != nullptr) {
    if (header->b_descriptor_type == USB_DT_INTERFACE) {
      const auto* intf_desc = reinterpret_cast<const usb_interface_descriptor_t*>(
          usb_desc_iter_get_structure(&header_iter, sizeof(usb_interface_descriptor_t)));
      if (!intf_desc) {
        zxlogf(ERROR, "Malformed USB descriptor detected!");
        return ZX_ERR_INTERNAL;
      }
      if (intf_desc->b_alternate_setting == 0) {
        zx_status_t status = interface->ConfigureEndpoints(intf_desc->b_interface_number, 0);
        if (status != ZX_OK) {
          return status;
        }
      }
    }
    if (!usb_desc_iter_advance(&header_iter)) {
      // This should not happen
      zxlogf(ERROR, "Malformed USB descriptor detected!");
      return ZX_ERR_INTERNAL;
    }
  }

  *out_interface = interface;
  return ZX_OK;
}

zx_status_t UsbInterface::Init(const void* descriptors, size_t desc_length,
                               uint8_t last_interface_id, uint8_t usb_class, uint8_t usb_subclass,
                               uint8_t usb_protocol) {
  fbl::AllocChecker ac;
  auto desc_bytes = new (&ac) uint8_t[desc_length];
  if (!ac.check()) {
    return ZX_ERR_NO_MEMORY;
  }
  memcpy(desc_bytes, descriptors, desc_length);
  descriptors_.reset(desc_bytes, desc_length);
  last_interface_id_ = last_interface_id;
  usb_class_ = usb_class;
  usb_subclass_ = usb_subclass;
  usb_protocol_ = usb_protocol;

  return ZX_OK;
}

zx_status_t UsbInterface::DdkGetProtocol(uint32_t proto_id, void* protocol) {
  switch (proto_id) {
    case ZX_PROTOCOL_USB: {
      auto* proto = static_cast<usb_protocol_t*>(protocol);
      proto->ctx = this;
      proto->ops = &usb_protocol_ops_;
      return ZX_OK;
    }
    case ZX_PROTOCOL_USB_COMPOSITE: {
      auto* proto = static_cast<usb_composite_protocol_t*>(protocol);
      proto->ctx = this;
      proto->ops = &usb_composite_protocol_ops_;
      return ZX_OK;
    }
    default:
      return ZX_ERR_NOT_SUPPORTED;
  }
}

void UsbInterface::DdkRelease() {
  // Release the reference now that devmgr no longer has a pointer to this object.
  __UNUSED bool dummy = Release();
}

// for determining index into active_endpoints[]
// b_endpoint_address has 4 lower order bits, plus high bit to signify direction
// shift high bit to bit 4 so index is in range 0 - 31.
static inline uint8_t GetEndpointIndex(const usb_endpoint_descriptor_t* ep) {
  return static_cast<uint8_t>(((ep)->b_endpoint_address & 0x0F) | ((ep)->b_endpoint_address >> 3));
}

zx_status_t UsbInterface::ConfigureEndpoints(uint8_t interface_id, uint8_t alt_setting) {
  usb_endpoint_descriptor_t* new_endpoints[USB_MAX_EPS] = {};
  bool interface_endpoints[USB_MAX_EPS] = {};
  zx_status_t status = ZX_OK;

  // iterate through our descriptors to find which endpoints should be active
  usb_desc_iter_t header_iter;
  status = usb_desc_iter_init_unowned(reinterpret_cast<void*>(descriptors_.data()),
                                      descriptors_.size(), &header_iter);
  if (status != ZX_OK) {
    zxlogf(ERROR, "Could not initialize iterator");
    return status;
  }
  int cur_interface = -1;

  bool enable_endpoints = false;
  usb_descriptor_header_t* header;
  while ((header = usb_desc_iter_peek(&header_iter)) != nullptr) {
    if (header->b_descriptor_type == USB_DT_INTERFACE) {
      const auto* intf_desc = reinterpret_cast<const usb_interface_descriptor_t*>(
          usb_desc_iter_get_structure(&header_iter, sizeof(usb_interface_descriptor_t)));
      if (!intf_desc) {
        zxlogf(ERROR, "Malformed USB descriptor detected!");
        return ZX_ERR_INTERNAL;
      }
      cur_interface = intf_desc->b_interface_number;
      enable_endpoints = (intf_desc->b_alternate_setting == alt_setting);
    } else if (header->b_descriptor_type == USB_DT_ENDPOINT && cur_interface == interface_id) {
      auto* ep = reinterpret_cast<usb_endpoint_descriptor_t*>(
          usb_desc_iter_get_structure(&header_iter, sizeof(usb_endpoint_descriptor_t)));
      if (!ep) {
        zxlogf(ERROR, "Malformed USB descriptor detected!");
        return ZX_ERR_INTERNAL;
      }
      auto ep_index = GetEndpointIndex(ep);
      interface_endpoints[ep_index] = true;
      if (enable_endpoints) {
        new_endpoints[ep_index] = ep;
      }
    }
    if (!usb_desc_iter_advance(&header_iter)) {
      // This should not happen
      zxlogf(ERROR, "Malformed USB descriptor detected!");
      return ZX_ERR_INTERNAL;
    }
  }

  // update to new set of endpoints
  // FIXME - how do we recover if we fail half way through processing the endpoints?
  for (size_t i = 0; i < countof(new_endpoints); i++) {
    if (interface_endpoints[i]) {
      auto* old_ep = active_endpoints_[i];
      auto* new_ep = new_endpoints[i];
      if (old_ep != new_ep) {
        if (old_ep) {
          zx_status_t ret = usb_.EnableEndpoint(old_ep, nullptr, false);
          if (ret != ZX_OK)
            status = ret;
        }
        if (new_ep) {
          usb_ss_ep_comp_descriptor_t* ss_comp_desc = nullptr;
          usb_descriptor_header_t* next =
              (usb_descriptor_header_t*)((uint8_t*)new_ep + new_ep->b_length);
          if (next + sizeof(*ss_comp_desc) <=
                  reinterpret_cast<usb_descriptor_header_t*>(header_iter.desc_end) &&
              next->b_descriptor_type == USB_DT_SS_EP_COMPANION) {
            ss_comp_desc = (usb_ss_ep_comp_descriptor_t*)next;
          }
          zx_status_t ret = usb_.EnableEndpoint(new_ep, ss_comp_desc, true);
          if (ret != ZX_OK) {
            status = ret;
          }
        }
        active_endpoints_[i] = new_ep;
      }
    }
  }
  return status;
}

zx_status_t UsbInterface::UsbControlOut(uint8_t request_type, uint8_t request, uint16_t value,
                                        uint16_t index, zx_time_t timeout,
                                        const uint8_t* write_buffer, size_t write_size) {
  return usb_.ControlOut(request_type, request, value, index, timeout, write_buffer, write_size);
}

zx_status_t UsbInterface::UsbControlIn(uint8_t request_type, uint8_t request, uint16_t value,
                                       uint16_t index, zx_time_t timeout, uint8_t* out_read_buffer,
                                       size_t read_size, size_t* out_read_actual) {
  return usb_.ControlIn(request_type, request, value, index, timeout, out_read_buffer, read_size,
                        out_read_actual);
}

void UsbInterface::UsbRequestQueue(usb_request_t* usb_request,
                                   const usb_request_complete_callback_t* complete_cb) {
  usb_.RequestQueue(usb_request, complete_cb);
}

usb_speed_t UsbInterface::UsbGetSpeed() { return usb_.GetSpeed(); }

zx_status_t UsbInterface::UsbSetInterface(uint8_t interface_number, uint8_t alt_setting) {
  return composite_->SetInterface(interface_number, alt_setting);
}

uint8_t UsbInterface::UsbGetConfiguration() { return usb_.GetConfiguration(); }

zx_status_t UsbInterface::UsbSetConfiguration(uint8_t configuration) {
  return usb_.SetConfiguration(configuration);
}

zx_status_t UsbInterface::UsbEnableEndpoint(const usb_endpoint_descriptor_t* ep_desc,
                                            const usb_ss_ep_comp_descriptor_t* ss_com_desc,
                                            bool enable) {
  return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t UsbInterface::UsbResetEndpoint(uint8_t ep_address) {
  return usb_.ResetEndpoint(ep_address);
}

zx_status_t UsbInterface::UsbResetDevice() { return usb_.ResetDevice(); }

size_t UsbInterface::UsbGetMaxTransferSize(uint8_t ep_address) {
  return usb_.GetMaxTransferSize(ep_address);
}

uint32_t UsbInterface::UsbGetDeviceId() { return usb_.GetDeviceId(); }

void UsbInterface::UsbGetDeviceDescriptor(usb_device_descriptor_t* out_desc) {
  return usb_.GetDeviceDescriptor(out_desc);
}

zx_status_t UsbInterface::UsbGetConfigurationDescriptorLength(uint8_t configuration,
                                                              size_t* out_length) {
  return usb_.GetConfigurationDescriptorLength(configuration, out_length);
}

zx_status_t UsbInterface::UsbGetConfigurationDescriptor(uint8_t configuration,
                                                        uint8_t* out_desc_buffer, size_t desc_size,
                                                        size_t* out_desc_actual) {
  return usb_.GetConfigurationDescriptor(configuration, out_desc_buffer, desc_size,
                                         out_desc_actual);
}

size_t UsbInterface::UsbGetDescriptorsLength() { return descriptors_.size(); }

void UsbInterface::UsbGetDescriptors(uint8_t* out_descs_buffer, size_t descs_size,
                                     size_t* out_descs_actual) {
  size_t length = descriptors_.size();
  if (length > descs_size) {
    length = descs_size;
  }
  memcpy(out_descs_buffer, descriptors_.data(), length);
  *out_descs_actual = length;
}

size_t UsbInterface::UsbCompositeGetAdditionalDescriptorLength() {
  auto* config = composite_->GetConfigurationDescriptor();
  usb_desc_iter_t header_iter;
  auto status = usb_desc_iter_init_unowned(const_cast<usb_configuration_descriptor_t*>(config),
                                           config->w_total_length, &header_iter);
  if (status != ZX_OK) {
    zxlogf(ERROR, "Could not init iterator %d", status);
    return status;
  }
  if (!usb_desc_iter_advance(&header_iter)) {
    zxlogf(ERROR, "Could not advance iterator");
    return ZX_ERR_INTERNAL;
  }

  const usb_interface_descriptor_t* interface = nullptr;
  usb_descriptor_header_t* header;
  while ((header = usb_desc_iter_peek(&header_iter)) != nullptr) {
    if (header->b_descriptor_type == USB_DT_INTERFACE) {
      auto* test_intf = reinterpret_cast<usb_interface_descriptor_t*>(
          usb_desc_iter_get_structure(&header_iter, sizeof(usb_interface_descriptor_t)));
      if (!test_intf) {
        zxlogf(ERROR, "Malformed USB descriptor detected!");
        return ZX_ERR_INTERNAL;
      }
      // We are only interested in descriptors past the last stored descriptor
      // for the current interface.
      if (test_intf->b_alternate_setting == 0 &&
          test_intf->b_interface_number > last_interface_id_) {
        interface = test_intf;
        break;
      }
    }
    if (!usb_desc_iter_advance(&header_iter)) {
      // This should not happen
      zxlogf(ERROR, "Malformed USB descriptor detected!");
      return ZX_ERR_INTERNAL;
    }
  }
  if (!interface) {
    return 0;
  }
  size_t ret =
      reinterpret_cast<uintptr_t>(header_iter.desc_end) - reinterpret_cast<uintptr_t>(interface);
  return ret;
}

zx_status_t UsbInterface::UsbCompositeGetAdditionalDescriptorList(uint8_t* out_desc_list,
                                                                  size_t desc_count,
                                                                  size_t* out_desc_actual) {
  return composite_->GetAdditionalDescriptorList(last_interface_id_, out_desc_list, desc_count,
                                                 out_desc_actual);
}

zx_status_t UsbInterface::UsbGetStringDescriptor(uint8_t desc_id, uint16_t lang_id,
                                                 uint16_t* out_lang_id, uint8_t* out_string_buffer,
                                                 size_t string_size, size_t* out_string_actual) {
  return usb_.GetStringDescriptor(desc_id, lang_id, out_lang_id, out_string_buffer, string_size,
                                  out_string_actual);
}

zx_status_t UsbInterface::UsbCancelAll(uint8_t ep_address) { return usb_.CancelAll(ep_address); }

uint64_t UsbInterface::UsbGetCurrentFrame() { return usb_.GetCurrentFrame(); }

size_t UsbInterface::UsbGetRequestSize() { return usb_.GetRequestSize(); }

zx_status_t UsbInterface::UsbCompositeClaimInterface(const usb_interface_descriptor_t* desc,
                                                     uint32_t length) {
  auto status = composite_->ClaimInterface(desc->b_interface_number);
  if (status != ZX_OK) {
    return status;
  }
  // Copy claimed interface descriptors to end of descriptor array.
  fbl::AllocChecker ac;
  size_t old_length = descriptors_.size();
  size_t new_length = old_length + length;
  auto* new_descriptors = new (&ac) uint8_t[new_length];
  if (!ac.check()) {
    return ZX_ERR_NO_MEMORY;
  }
  memcpy(new_descriptors, descriptors_.data(), old_length);
  memcpy(new_descriptors + old_length, desc, length);
  descriptors_.reset(new_descriptors, new_length);

  if (desc->b_interface_number > last_interface_id_) {
    last_interface_id_ = desc->b_interface_number;
  }
  return ZX_OK;
}

bool UsbInterface::ContainsInterface(uint8_t interface_id) {
  usb_desc_iter_t header_iter;
  auto status = usb_desc_iter_init_unowned(reinterpret_cast<void*>(descriptors_.data()),
                                           descriptors_.size(), &header_iter);
  if (status != ZX_OK) {
    zxlogf(ERROR, "Could not initialize iterator");
    return false;
  }

  usb_descriptor_header_t* header;
  while ((header = usb_desc_iter_peek(&header_iter)) != nullptr) {
    if (header->b_descriptor_type == USB_DT_INTERFACE) {
      const auto* intf_desc = reinterpret_cast<const usb_interface_descriptor_t*>(
          usb_desc_iter_get_structure(&header_iter, sizeof(usb_interface_descriptor_t)));
      if (!intf_desc) {
        zxlogf(ERROR, "Malformed USB descriptor detected!");
        return false;
      }
      if (intf_desc->b_interface_number == interface_id) {
        return true;
      }
    }
    if (!usb_desc_iter_advance(&header_iter)) {
      // This should not happen
      zxlogf(ERROR, "Malformed USB descriptor detected!");
      return false;
    }
  }
  return false;
}

zx_status_t UsbInterface::SetAltSetting(uint8_t interface_id, uint8_t alt_setting) {
  zx_status_t status = ConfigureEndpoints(interface_id, alt_setting);
  if (status != ZX_OK) {
    return status;
  }

  return UsbControlOut(USB_DIR_OUT | USB_TYPE_STANDARD | USB_RECIP_INTERFACE, USB_REQ_SET_INTERFACE,
                       alt_setting, interface_id, ZX_TIME_INFINITE, nullptr, 0);
}

}  // namespace usb_composite
