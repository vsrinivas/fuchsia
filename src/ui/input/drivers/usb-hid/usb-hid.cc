// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "usb-hid.h"

#include <lib/sync/completion.h>
#include <stdlib.h>
#include <string.h>
#include <zircon/hw/usb/hid.h>
#include <zircon/status.h>
#include <zircon/types.h>

#include <thread>

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/protocol/hidbus.h>
#include <ddk/protocol/usb.h>
#include <fbl/auto_lock.h>
#include <pretty/hexdump.h>
#include <usb/usb-request.h>
#include <usb/usb.h>

namespace usb_hid {

#define to_usb_hid(d) containerof(d, usb_hid_device_t, hiddev)

// This driver binds on any USB device that exposes HID reports. It passes the
// reports to the HID driver by implementing the HidBus protocol.

void UsbHidbus::UsbInterruptCallback(usb_request_t* req) {
  // TODO use usb request copyfrom instead of mmap
  void* buffer;
  zx_status_t status = usb_request_mmap(req, &buffer);
  if (status != ZX_OK) {
    zxlogf(ERROR, "usb-hid: usb_request_mmap failed: %s", zx_status_get_string(status));
    return;
  }
  zxlogf(TRACE, "usb-hid: callback request status %d", req->response.status);
  if (zxlog_level_enabled(TRACE)) {
    hexdump(buffer, req->response.actual);
  }

  bool requeue = true;
  fbl::AutoLock lock(&hidbus_ifc_lock_);
  switch (req->response.status) {
    case ZX_ERR_IO_NOT_PRESENT:
      requeue = false;
      break;
    case ZX_OK:
      if (ifc_.is_valid()) {
        ifc_.IoQueue(buffer, req->response.actual, zx_clock_get_monotonic());
      }
      break;
    default:
      zxlogf(ERROR, "usb-hid: unknown interrupt status %d; not requeuing req",
             req->response.status);
      requeue = false;
      break;
  }

  if (requeue) {
    usb_request_complete_t complete = {
        .callback =
            [](void* ctx, usb_request_t* request) {
              static_cast<UsbHidbus*>(ctx)->UsbInterruptCallback(request);
            },
        .ctx = this,
    };
    usb_.RequestQueue(req, &complete);
  } else {
    req_queued_ = false;
  }
}

zx_status_t UsbHidbus::HidbusQuery(uint32_t options, hid_info_t* info) {
  if (!info) {
    return ZX_ERR_INVALID_ARGS;
  }
  info->dev_num = info_.dev_num;
  info->device_class = info_.device_class;
  info->boot_device = info_.boot_device;

  info->vendor_id = info_.vendor_id;
  info->product_id = info_.product_id;
  info->version = info_.version;
  return ZX_OK;
}

zx_status_t UsbHidbus::HidbusStart(const hidbus_ifc_protocol_t* ifc) {
  fbl::AutoLock lock(&hidbus_ifc_lock_);
  if (ifc_.is_valid()) {
    return ZX_ERR_ALREADY_BOUND;
  }
  ifc_ = ifc;
  if (!req_queued_) {
    req_queued_ = true;
    usb_request_complete_t complete = {
        .callback =
            [](void* ctx, usb_request_t* request) {
              static_cast<UsbHidbus*>(ctx)->UsbInterruptCallback(request);
            },
        .ctx = this,
    };
    usb_.RequestQueue(req_, &complete);
  }
  return ZX_OK;
}

void UsbHidbus::HidbusStop() {
  // TODO(tkilbourn) set flag to stop requeueing the interrupt request when we start using
  // this callback
  fbl::AutoLock lock(&hidbus_ifc_lock_);
  ifc_.clear();
}

zx_status_t UsbHidbus::UsbHidControlIn(uint8_t req_type, uint8_t request, uint16_t value,
                                       uint16_t index, void* data, size_t length,
                                       size_t* out_length) {
  zx_status_t status;
  status =
      usb_.ControlIn(req_type, request, value, index, ZX_TIME_INFINITE, data, length, out_length);
  if (status == ZX_ERR_IO_REFUSED || status == ZX_ERR_IO_INVALID) {
    status = usb_.ResetEndpoint(0);
  }
  return status;
}

zx_status_t UsbHidbus::UsbHidControlOut(uint8_t req_type, uint8_t request, uint16_t value,
                                        uint16_t index, const void* data, size_t length,
                                        size_t* out_length) {
  zx_status_t status;
  status = usb_.ControlOut(req_type, request, value, index, ZX_TIME_INFINITE, data, length);
  if (status == ZX_ERR_IO_REFUSED || status == ZX_ERR_IO_INVALID) {
    status = usb_.ResetEndpoint(0);
  }
  return status;
}
zx_status_t UsbHidbus::HidbusGetDescriptor(hid_description_type_t desc_type, void* out_data_buffer,
                                           size_t data_size, size_t* out_data_actual) {
  int desc_idx = -1;
  for (int i = 0; i < hid_desc_->bNumDescriptors; i++) {
    if (hid_desc_->descriptors[i].bDescriptorType == desc_type) {
      desc_idx = i;
      break;
    }
  }
  if (desc_idx < 0) {
    return ZX_ERR_NOT_FOUND;
  }

  size_t desc_len = hid_desc_->descriptors[desc_idx].wDescriptorLength;
  if (data_size < desc_len) {
    return ZX_ERR_BUFFER_TOO_SMALL;
  }

  zx_status_t status =
      UsbHidControlIn(USB_DIR_IN | USB_TYPE_STANDARD | USB_RECIP_INTERFACE, USB_REQ_GET_DESCRIPTOR,
                      static_cast<uint16_t>(desc_type << 8), interface_, out_data_buffer, desc_len,
                      out_data_actual);
  if (status < 0) {
    zxlogf(ERROR, "usb-hid: error reading report descriptor 0x%02x: %d", desc_type, status);
  }
  return status;
}

zx_status_t UsbHidbus::HidbusGetReport(uint8_t rpt_type, uint8_t rpt_id, void* data, size_t len,
                                       size_t* out_len) {
  if (out_len == NULL) {
    return ZX_ERR_INVALID_ARGS;
  }
  return UsbHidControlIn(USB_DIR_IN | USB_TYPE_CLASS | USB_RECIP_INTERFACE, USB_HID_GET_REPORT,
                         static_cast<uint16_t>(rpt_type << 8 | rpt_id), interface_, data, len,
                         out_len);
}

zx_status_t UsbHidbus::HidbusSetReport(uint8_t rpt_type, uint8_t rpt_id, const void* data,
                                       size_t len) {
  if (has_endptout_) {
    sync_completion_reset(&set_report_complete_);
    usb_request_complete_t complete = {
        .callback =
            [](void* ctx, usb_request_t* request) {
              sync_completion_signal(&static_cast<UsbHidbus*>(ctx)->set_report_complete_);
            },
        .ctx = this,
    };

    request_out_->header.length = len;
    if (len > endptout_max_size_) {
      return ZX_ERR_BUFFER_TOO_SMALL;
    }
    usb_request_copy_to(request_out_, data, len, 0);
    usb_.RequestQueue(request_out_, &complete);
    auto status = sync_completion_wait(&set_report_complete_, ZX_TIME_INFINITE);
    return status;
  }
  return UsbHidControlOut(USB_DIR_OUT | USB_TYPE_CLASS | USB_RECIP_INTERFACE, USB_HID_SET_REPORT,
                          (static_cast<uint16_t>(rpt_type << 8 | rpt_id)), interface_, data, len,
                          NULL);
}

zx_status_t UsbHidbus::HidbusGetIdle(uint8_t rpt_id, uint8_t* duration) {
  return UsbHidControlIn(USB_DIR_IN | USB_TYPE_CLASS | USB_RECIP_INTERFACE, USB_HID_GET_IDLE,
                         rpt_id, interface_, duration, sizeof(*duration), NULL);
}

zx_status_t UsbHidbus::HidbusSetIdle(uint8_t rpt_id, uint8_t duration) {
  return UsbHidControlOut(USB_DIR_OUT | USB_TYPE_CLASS | USB_RECIP_INTERFACE, USB_HID_SET_IDLE,
                          static_cast<uint16_t>((duration << 8) | rpt_id), interface_, NULL, 0,
                          NULL);
}

zx_status_t UsbHidbus::HidbusGetProtocol(uint8_t* protocol) {
  return UsbHidControlIn(USB_DIR_IN | USB_TYPE_CLASS | USB_RECIP_INTERFACE, USB_HID_GET_PROTOCOL, 0,
                         interface_, protocol, sizeof(*protocol), NULL);
}

zx_status_t UsbHidbus::HidbusSetProtocol(uint8_t protocol) {
  return UsbHidControlOut(USB_DIR_OUT | USB_TYPE_CLASS | USB_RECIP_INTERFACE, USB_HID_SET_PROTOCOL,
                          protocol, interface_, NULL, 0, NULL);
}

void UsbHidbus::DdkUnbind(ddk::UnbindTxn txn) {
  unbind_thread_ = std::thread([this, txn = std::move(txn)]() mutable {
    usb_.CancelAll(endptin_address_);
    usb_.CancelAll(endptout_address_);
    txn.Reply();
  });
}

void UsbHidbus::DdkRelease() {
  if (req_) {
    usb_request_release(req_);
  }
  usb_desc_iter_release(&desc_iter_);
  unbind_thread_.join();
  delete this;
}

void UsbHidbus::FindDescriptors(usb::Interface interface, usb_hid_descriptor_t** hid_desc,
                                const usb_endpoint_descriptor_t** endptin,
                                const usb_endpoint_descriptor_t** endptout) {
  for (auto& descriptor : interface.GetDescriptorList()) {
    if (descriptor.bDescriptorType == USB_DT_HID) {
      *hid_desc = (usb_hid_descriptor_t*)&descriptor;
    } else if (descriptor.bDescriptorType == USB_DT_ENDPOINT) {
      if (usb_ep_direction((usb_endpoint_descriptor_t*)&descriptor) == USB_ENDPOINT_IN &&
          usb_ep_type((usb_endpoint_descriptor_t*)&descriptor) == USB_ENDPOINT_INTERRUPT) {
        *endptin = (usb_endpoint_descriptor_t*)&descriptor;
      } else if (usb_ep_direction((usb_endpoint_descriptor_t*)&descriptor) == USB_ENDPOINT_OUT &&
                 usb_ep_type((usb_endpoint_descriptor_t*)&descriptor) == USB_ENDPOINT_INTERRUPT) {
        *endptout = (usb_endpoint_descriptor_t*)&descriptor;
      }
    }
  }
}

zx_status_t UsbHidbus::Bind(ddk::UsbProtocolClient usbhid) {
  zx_status_t status;
  usb_ = usbhid;
  parent_req_size_ = usb_.GetRequestSize();
  status = usb::InterfaceList::Create(usb_, true, &usb_interface_list_);
  if (status != ZX_OK) {
    return status;
  }

  usb_hid_descriptor_t* hid_desc = NULL;
  const usb_endpoint_descriptor_t* endptin = NULL;
  const usb_endpoint_descriptor_t* endptout = NULL;
  auto interface = *usb_interface_list_->begin();

  FindDescriptors(interface, &hid_desc, &endptin, &endptout);
  if (!hid_desc) {
    status = ZX_ERR_NOT_SUPPORTED;
    return status;
  }
  if (!endptin) {
    status = ZX_ERR_NOT_SUPPORTED;
    return status;
  }
  hid_desc_ = hid_desc;
  endptin_address_ = endptin->bEndpointAddress;

  if (endptout) {
    endptout_address_ = endptout->bEndpointAddress;
    has_endptout_ = true;
    endptout_max_size_ = usb_ep_max_packet(endptout);
    status = usb_request_alloc(&request_out_, endptout_max_size_, endptout->bEndpointAddress,
                               parent_req_size_);
  }

  interface_ = info_.dev_num = interface.descriptor()->bInterfaceNumber;
  info_.boot_device = interface.descriptor()->bInterfaceSubClass == USB_HID_SUBCLASS_BOOT;
  info_.device_class = HID_DEVICE_CLASS_OTHER;
  if (interface.descriptor()->bInterfaceProtocol == USB_HID_PROTOCOL_KBD) {
    info_.device_class = HID_DEVICE_CLASS_KBD;
  } else if (interface.descriptor()->bInterfaceProtocol == USB_HID_PROTOCOL_MOUSE) {
    info_.device_class = HID_DEVICE_CLASS_POINTER;
  }

  status = usb_request_alloc(&req_, usb_ep_max_packet(endptin), endptin->bEndpointAddress,
                             parent_req_size_);
  if (status != ZX_OK) {
    status = ZX_ERR_NO_MEMORY;
    return status;
  }

  status = DdkAdd("usb-hid");
  if (status != ZX_OK) {
    return status;
  }

  return ZX_OK;
}

static zx_status_t usb_hid_bind(void* ctx, zx_device_t* parent) {
  auto usbHid = std::make_unique<UsbHidbus>(parent);

  ddk::UsbProtocolClient usb;
  zx_status_t status = device_get_protocol(parent, ZX_PROTOCOL_USB, &usb);

  if (status != ZX_OK) {
    return status;
  }

  status = usbHid->Bind(usb);
  if (status == ZX_OK) {
    // devmgr is now in charge of the memory for dev.
    __UNUSED auto ptr = usbHid.release();
  }
  return status;
}

static zx_driver_ops_t usb_hid_driver_ops = []() {
  zx_driver_ops_t ops = {};
  ops.version = DRIVER_OPS_VERSION;
  ops.bind = usb_hid_bind;
  return ops;
}();

}  // namespace usb_hid

// clang-format off
ZIRCON_DRIVER_BEGIN(usb_hid, usb_hid::usb_hid_driver_ops, "zircon", "0.1", 2)
  BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_USB),
  BI_MATCH_IF(EQ, BIND_USB_CLASS, USB_CLASS_HID),
ZIRCON_DRIVER_END(usb_hid)
    // clang-format on
