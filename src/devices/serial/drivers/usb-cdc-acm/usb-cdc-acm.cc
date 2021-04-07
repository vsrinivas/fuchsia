// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "usb-cdc-acm.h"

#include <assert.h>
#include <fuchsia/hardware/serial/c/fidl.h>
#include <lib/ddk/debug.h>
#include <lib/ddk/driver.h>
#include <zircon/hw/usb.h>
#include <zircon/hw/usb/cdc.h>

#include <fbl/alloc_checker.h>
#include <usb/request-cpp.h>
#include <usb/usb-request.h>
#include <usb/usb.h>

#include "src/devices/serial/drivers/usb-cdc-acm/cdc_acm_bind.h"

namespace usb_cdc_acm_serial {

namespace {

constexpr int32_t kReadRequestCount = 8;
constexpr int32_t kWriteRequestCount = 8;

constexpr uint32_t kDefaultBaudRate = 115200;
constexpr uint32_t kDefaultConfig = SERIAL_DATA_BITS_8 | SERIAL_STOP_BITS_1 | SERIAL_PARITY_NONE;

constexpr uint32_t kUsbBufferSize = 2048;

constexpr uint32_t kUsbCdcAcmSetLineCoding = 0x20;
constexpr uint32_t kUsbCdcAcmGetLineCoding = 0x21;

}  // namespace

struct usb_cdc_acm_line_coding_t {
  uint32_t dwDTERate;
  uint8_t bCharFormat;
  uint8_t bParityType;
  uint8_t bDataBits;
} __PACKED;

void UsbCdcAcmDevice::NotifyCallback() {
  if (need_to_notify_cb_ == true) {
    need_to_notify_cb_ = false;
    if (notify_cb_.callback) {
      notify_cb_.callback(notify_cb_.ctx, state_);
    }
  }
}

void UsbCdcAcmDevice::CheckStateLocked() {
  uint32_t state = 0;
  state |= free_write_queue_.is_empty() ? 0 : SERIAL_STATE_WRITABLE;
  state |= completed_reads_queue_.is_empty() ? 0 : SERIAL_STATE_READABLE;

  if (state != state_) {
    state_ = state;
    need_to_notify_cb_ = true;
  }
}

zx_status_t UsbCdcAcmDevice::DdkRead(void* data, size_t len, zx_off_t /*off*/, size_t* actual) {
  size_t bytes_copied = 0;
  size_t offset = read_offset_;
  auto* buffer = static_cast<uint8_t*>(data);

  fbl::AutoLock lock(&lock_);

  while (bytes_copied < len) {
    std::optional<usb::Request<>> req = completed_reads_queue_.pop();
    if (!req) {
      break;
    }

    // Skip invalid or empty responses.
    if (req->request()->response.status == ZX_OK && req->request()->response.actual > 0) {
      // |offset| will always be zero if a response is being read for the first time. It can only be
      // non-zero if |req| was re-queued below, which should guarantee that |offset| is within the
      // response length.
      assert(offset < req->request()->response.actual);

      // Copy as many bytes as available or as needed from the first request.
      size_t to_copy = req->request()->response.actual - offset;
      if ((to_copy + bytes_copied) > len) {
        to_copy = len - bytes_copied;
      }
      size_t result = req->CopyFrom(&buffer[bytes_copied], to_copy, offset);
      ZX_ASSERT(result == to_copy);
      bytes_copied += to_copy;

      // If we aren't reading the whole request, put it back on the front of the completed queue and
      // mark the offset into it for the next read.
      if ((to_copy + offset) < req->request()->response.actual) {
        offset = offset + to_copy;
        completed_reads_queue_.push_next(*std::move(req));
        break;
      }
    }

    usb_client_.RequestQueue(req->take(), &read_request_complete_);
    offset = 0;
  }

  CheckStateLocked();

  // Store the offset into the current request for the next read.
  read_offset_ = offset;
  *actual = bytes_copied;

  lock.release();
  NotifyCallback();

  return ZX_OK;
}

zx_status_t UsbCdcAcmDevice::DdkWrite(const void* buf, size_t length, zx_off_t /*off*/,
                                      size_t* actual) {
  fbl::AutoLock lock(&lock_);

  std::optional<usb::Request<>> req = free_write_queue_.pop();
  if (!req) {
    *actual = 0;
    return ZX_ERR_SHOULD_WAIT;
  }

  *actual = req->CopyTo(buf, length, 0);
  req->request()->header.length = length;

  usb_client_.RequestQueue(req->take(), &write_request_complete_);
  CheckStateLocked();

  lock.release();
  NotifyCallback();

  return ZX_OK;
}

void UsbCdcAcmDevice::DdkUnbind(ddk::UnbindTxn txn) {
  cancel_thread_ = std::thread([this, unbind_txn = std::move(txn)]() mutable {
    usb_client_.CancelAll(bulk_in_addr_);
    usb_client_.CancelAll(bulk_out_addr_);
    unbind_txn.Reply();
  });
}

void UsbCdcAcmDevice::DdkRelease() {
  cancel_thread_.join();
  delete this;
}

zx_status_t UsbCdcAcmDevice::SerialImplGetInfo(serial_port_info_t* info) {
  memcpy(info, &serial_port_info_, sizeof(*info));
  return ZX_OK;
}

zx_status_t UsbCdcAcmDevice::SerialImplConfig(uint32_t baud_rate, uint32_t flags) {
  if (baud_rate_ != baud_rate || flags != config_flags_) {
    return ConfigureDevice(baud_rate, flags);
  }
  return ZX_OK;
}

zx_status_t UsbCdcAcmDevice::SerialImplEnable(bool enable) {
  enabled_ = enable;
  return ZX_OK;
}

zx_status_t UsbCdcAcmDevice::SerialImplRead(uint8_t* data, size_t len, size_t* actual) {
  zx_status_t status = DdkRead(data, len, 0, actual);
  if (status == ZX_OK && actual == nullptr) {
    return ZX_ERR_SHOULD_WAIT;
  }
  return status;
}

zx_status_t UsbCdcAcmDevice::SerialImplWrite(const uint8_t* buf, size_t length, size_t* actual) {
  return DdkWrite(buf, length, 0, actual);
}

zx_status_t UsbCdcAcmDevice::SerialImplSetNotifyCallback(const serial_notify_t* cb) {
  if (enabled_) {
    return ZX_ERR_BAD_STATE;
  }

  notify_cb_ = *cb;

  fbl::AutoLock lock(&lock_);
  CheckStateLocked();
  lock.release();
  NotifyCallback();

  return ZX_OK;
}

void UsbCdcAcmDevice::ReadComplete(usb_request_t* request) {
  usb::Request<> req(request, parent_req_size_);
  if (req.request()->response.status == ZX_ERR_IO_NOT_PRESENT) {
    zxlogf(INFO, "usb-cdc-acm: remote closed");
    return;
  }

  fbl::AutoLock lock(&lock_);

  if (req.request()->response.status == ZX_OK) {
    completed_reads_queue_.push(std::move(req));
    CheckStateLocked();
  } else {
    usb_client_.RequestQueue(req.take(), &read_request_complete_);
  }
  lock.release();
  NotifyCallback();
}

void UsbCdcAcmDevice::WriteComplete(usb_request_t* request) {
  usb::Request<> req(request, parent_req_size_);
  if (req.request()->response.status == ZX_ERR_IO_NOT_PRESENT) {
    zxlogf(INFO, "usb-cdc-acm: remote closed");
    return;
  }

  fbl::AutoLock lock(&lock_);

  free_write_queue_.push(std::move(req));
  CheckStateLocked();

  lock.release();
  NotifyCallback();
}

zx_status_t UsbCdcAcmDevice::ConfigureDevice(uint32_t baud_rate, uint32_t flags) {
  if (!usb_client_.is_valid()) {
    return ZX_ERR_INVALID_ARGS;
  }

  zx_status_t status = ZX_OK;

  usb_cdc_acm_line_coding_t coding;
  const bool baud_rate_only = flags & SERIAL_SET_BAUD_RATE_ONLY;
  if (baud_rate_only) {
    size_t coding_length;
    status = usb_client_.ControlIn(
        USB_DIR_IN | USB_TYPE_CLASS | USB_RECIP_INTERFACE, kUsbCdcAcmGetLineCoding, 0, 0,
        ZX_TIME_INFINITE, reinterpret_cast<uint8_t*>(&coding), sizeof(coding), &coding_length);
    if (coding_length != sizeof(coding)) {
      zxlogf(TRACE, "usb-cdc-acm: failed to fetch line coding");
    }
    if (status != ZX_OK) {
      return status;
    }
  } else {
    switch (flags & SERIAL_STOP_BITS_MASK) {
      case SERIAL_STOP_BITS_1:
        coding.bCharFormat = 0;
        break;
      case SERIAL_STOP_BITS_2:
        coding.bCharFormat = 2;
        break;
      default:
        return ZX_ERR_INVALID_ARGS;
    }
    switch (flags & SERIAL_PARITY_MASK) {
      case SERIAL_PARITY_NONE:
        coding.bParityType = 0;
        break;
      case SERIAL_PARITY_EVEN:
        coding.bParityType = 2;
        break;
      case SERIAL_PARITY_ODD:
        coding.bParityType = 1;
        break;
      default:
        return ZX_ERR_INVALID_ARGS;
    }
    switch (flags & SERIAL_DATA_BITS_MASK) {
      case SERIAL_DATA_BITS_5:
        coding.bDataBits = 5;
        break;
      case SERIAL_DATA_BITS_6:
        coding.bDataBits = 6;
        break;
      case SERIAL_DATA_BITS_7:
        coding.bDataBits = 7;
        break;
      case SERIAL_DATA_BITS_8:
        coding.bDataBits = 8;
        break;
      default:
        return ZX_ERR_INVALID_ARGS;
    }
  }

  coding.dwDTERate = baud_rate;

  status = usb_client_.ControlOut(USB_DIR_OUT | USB_TYPE_CLASS | USB_RECIP_INTERFACE,
                                  kUsbCdcAcmSetLineCoding, 0, 0, ZX_TIME_INFINITE,
                                  reinterpret_cast<uint8_t*>(&coding), sizeof(coding));

  if (status == ZX_OK) {
    baud_rate_ = baud_rate;
    if (!baud_rate_only) {
      config_flags_ = flags;
    }
  }
  return status;
}

zx_status_t UsbCdcAcmDevice::Bind() {
  zx_status_t status = ZX_OK;

  if (!usb_client_.is_valid()) {
    return ZX_ERR_PROTOCOL_NOT_SUPPORTED;
  }

  // Enumerate available interfaces and find bulk-in and bulk-out endpoints.
  std::optional<usb::InterfaceList> usb_interface_list;
  status = usb::InterfaceList::Create(usb_client_, true, &usb_interface_list);
  if (status != ZX_OK) {
    return status;
  }

  fbl::AutoLock lock(&lock_);
  uint8_t bulk_in_address = 0;
  uint8_t bulk_out_address = 0;

  for (auto interface : *usb_interface_list) {
    if (interface.descriptor()->b_num_endpoints > 1) {
      for (auto& endpoint : interface.GetEndpointList()) {
        if (usb_ep_type(&endpoint.descriptor) == USB_ENDPOINT_BULK) {
          if (usb_ep_direction(&endpoint.descriptor) == USB_ENDPOINT_IN) {
            bulk_in_address = endpoint.descriptor.b_endpoint_address;
          } else if (usb_ep_direction(&endpoint.descriptor) == USB_ENDPOINT_OUT) {
            bulk_out_address = endpoint.descriptor.b_endpoint_address;
          }
        }
      }
    }
  }

  if (!bulk_in_address || !bulk_out_address) {
    zxlogf(ERROR, "usb-cdc-acm: Bind() could not find bulk-in and bulk-out endpoints");
    return ZX_ERR_NOT_SUPPORTED;
  }

  bulk_in_addr_ = bulk_in_address;
  bulk_out_addr_ = bulk_out_address;
  parent_req_size_ = usb_client_.GetRequestSize();

  status = ConfigureDevice(kDefaultBaudRate, kDefaultConfig);
  if (status != ZX_OK) {
    zxlogf(ERROR, "usb-cdc-acm: failed to set default baud rate: %d", status);
    return status;
  }

  serial_port_info_.serial_class = fuchsia_hardware_serial_Class_GENERIC;

  status = DdkAdd("usb-cdc-acm");
  if (status != ZX_OK) {
    zxlogf(ERROR, "usb-cdc-acm: failed to create device: %d", status);
    return status;
  }

  // Create and immediately queue read requests after successfully adding the device.
  for (int i = 0; i < kReadRequestCount; i++) {
    std::optional<usb::Request<>> request;
    status = usb::Request<>::Alloc(&request, kUsbBufferSize, bulk_in_addr_, parent_req_size_);
    if (status != ZX_OK) {
      zxlogf(ERROR, "usb-cdc-acm: allocating reads failed %d", status);
      return status;
    }
    usb_client_.RequestQueue(request->take(), &read_request_complete_);
  }

  for (int i = 0; i < kWriteRequestCount; i++) {
    std::optional<usb::Request<>> request;
    status = usb::Request<>::Alloc(&request, kUsbBufferSize, bulk_out_addr_, parent_req_size_);
    if (status != ZX_OK) {
      zxlogf(ERROR, "usb-cdc-acm: allocating writes failed %d", status);
      return status;
    }
    free_write_queue_.push(*std::move(request));
  }

  return ZX_OK;
}

}  // namespace usb_cdc_acm_serial

namespace {

zx_status_t cdc_acm_bind(void* /*ctx*/, zx_device_t* device) {
  fbl::AllocChecker ac;
  auto dev = fbl::make_unique_checked<usb_cdc_acm_serial::UsbCdcAcmDevice>(&ac, device);
  if (!ac.check()) {
    return ZX_ERR_NO_MEMORY;
  }
  auto status = dev->Bind();
  if (status != ZX_OK) {
    zxlogf(INFO, "usb-cdc-acm: failed to add serial driver %d", status);
  }

  // Devmgr is now in charge of the memory for dev.
  __UNUSED auto ptr = dev.release();
  return status;
}

constexpr zx_driver_ops_t cdc_acm_driver_ops = []() {
  zx_driver_ops_t ops = {};
  ops.version = DRIVER_OPS_VERSION;
  ops.bind = cdc_acm_bind;
  return ops;
}();

}  // namespace

// clang-format off
ZIRCON_DRIVER(cdc_acm, cdc_acm_driver_ops, "zircon", "0.1");
// clang-form   at on
