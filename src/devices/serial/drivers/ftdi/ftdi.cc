// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ftdi.h"

#include <fcntl.h>
#include <fuchsia/hardware/serial/c/fidl.h>
#include <fuchsia/hardware/serialimpl/cpp/banjo.h>
#include <fuchsia/hardware/usb/c/banjo.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <threads.h>
#include <unistd.h>
#include <zircon/hw/usb.h>
#include <zircon/listnode.h>

#include <ddk/debug.h>
#include <ddk/driver.h>
#include <ddktl/device.h>
#include <ddktl/fidl.h>
#include <fbl/alloc_checker.h>
#include <fbl/auto_call.h>
#include <fbl/auto_lock.h>
#include <usb/usb-request.h>
#include <usb/usb.h>

#include "ftdi-i2c.h"
#include "src/devices/serial/drivers/ftdi/ftdi_bind.h"

#define FTDI_STATUS_SIZE 2
#define FTDI_RX_HEADER_SIZE 4

#define READ_REQ_COUNT 8
#define WRITE_REQ_COUNT 4
#define INTR_REQ_COUNT 4
#define USB_BUF_SIZE 2048
#define INTR_REQ_SIZE 4

#define FIFOSIZE 256
#define FIFOMASK (FIFOSIZE - 1)

namespace {

static zx_status_t FtdiBindFail(zx_status_t status) {
  zxlogf(ERROR, "ftdi_bind failed: %d", status);
  return status;
}

}  // namespace

namespace ftdi_serial {

void FtdiDevice::NotifyCallback() {
  if (need_to_notify_cb_ == true) {
    need_to_notify_cb_ = false;
    if (notify_cb_.callback) {
      notify_cb_.callback(notify_cb_.ctx, state_);
    }
  }
}

void FtdiDevice::CheckStateLocked() {
  uint32_t state = 0;

  state |= free_write_queue_.is_empty() ? 0 : SERIAL_STATE_WRITABLE;

  state |= completed_reads_queue_.is_empty() ? 0 : SERIAL_STATE_READABLE;

  if (state != state_) {
    state_ = state;
    need_to_notify_cb_ = true;
  }
}

void FtdiDevice::ReadComplete(usb_request_t* request) {
  usb::Request<> req(request, parent_req_size_);
  if (req.request()->response.status == ZX_ERR_IO_NOT_PRESENT) {
    zxlogf(INFO, "FTDI: remote closed");
    return;
  }

  fbl::AutoLock lock(&mutex_);

  if ((req.request()->response.status == ZX_OK) && (req.request()->response.actual > 2)) {
    completed_reads_queue_.push(std::move(req));
    CheckStateLocked();
  } else {
    usb_request_complete_t complete = {
        .callback =
            [](void* ctx, usb_request_t* request) {
              static_cast<FtdiDevice*>(ctx)->ReadComplete(request);
            },
        .ctx = this,
    };
    usb_client_.RequestQueue(req.take(), &complete);
  }
  lock.release();
  NotifyCallback();
}

void FtdiDevice::WriteComplete(usb_request_t* request) {
  usb::Request<> req(request, parent_req_size_);
  if (req.request()->response.status == ZX_ERR_IO_NOT_PRESENT) {
    return;
  }

  fbl::AutoLock lock(&mutex_);

  free_write_queue_.push(std::move(req));
  CheckStateLocked();

  lock.release();
  NotifyCallback();
}

zx_status_t FtdiDevice::CalcDividers(uint32_t* baudrate, uint32_t clock, uint32_t divisor,
                                     uint16_t* integer_div, uint16_t* fraction_div) {
  static constexpr uint8_t kFractionLookup[8] = {0, 3, 2, 4, 1, 5, 6, 7};

  uint32_t base_clock = clock / divisor;

  // Integer dividers of 1 and 0 are special cases.
  // 0 = base_clock and 1 = 2/3 of base clock.
  if (*baudrate >= base_clock) {
    // Return with max baud rate achievable.
    *fraction_div = 0;
    *integer_div = 0;
    *baudrate = base_clock;
  } else if (*baudrate >= (base_clock * 2) / 3) {
    *integer_div = 1;
    *fraction_div = 0;
    *baudrate = (base_clock * 2) / 3;
  } else {
    // Create a 28.4 fractional integer.
    uint32_t ratio = (base_clock * 16) / *baudrate;

    // Round up if needed.
    ratio++;
    ratio = ratio & 0xfffffffe;

    *baudrate = (base_clock << 4) / ratio;
    *integer_div = static_cast<uint16_t>(ratio >> 4);
    *fraction_div = kFractionLookup[(ratio >> 1) & 0x07];
  }
  return ZX_OK;
}

zx_status_t FtdiDevice::SerialImplWrite(const uint8_t* buf, size_t length, size_t* actual) {
  return DdkWrite(buf, length, 0, actual);
}

zx_status_t FtdiDevice::DdkWrite(const void* buf, size_t length, zx_off_t off, size_t* actual) {
  zx_status_t status = ZX_OK;

  fbl::AutoLock lock(&mutex_);

  std::optional<usb::Request<>> req = free_write_queue_.pop();
  if (!req) {
    status = ZX_ERR_SHOULD_WAIT;
    *actual = 0;
    return status;
  }

  *actual = req->CopyTo(buf, length, 0);
  req->request()->header.length = length;

  usb_request_complete_t complete = {
      .callback =
          [](void* ctx, usb_request_t* request) {
            static_cast<FtdiDevice*>(ctx)->WriteComplete(request);
          },
      .ctx = this,
  };
  usb_client_.RequestQueue(req->take(), &complete);
  CheckStateLocked();

  lock.release();
  NotifyCallback();

  return status;
}

zx_status_t FtdiDevice::SerialImplRead(uint8_t* data, size_t len, size_t* actual) {
  zx_status_t status = DdkRead(data, len, 0, actual);
  if (status == ZX_OK && (actual == 0)) {
    return ZX_ERR_SHOULD_WAIT;
  }
  return status;
}

zx_status_t FtdiDevice::DdkRead(void* data, size_t len, zx_off_t off, size_t* actual) {
  size_t bytes_copied = 0;
  size_t offset = read_offset_;
  uint8_t* buffer = static_cast<uint8_t*>(data);

  usb_request_complete_t complete = {
      .callback =
          [](void* ctx, usb_request_t* request) {
            static_cast<FtdiDevice*>(ctx)->ReadComplete(request);
          },
      .ctx = this,
  };

  fbl::AutoLock lock(&mutex_);

  while (bytes_copied < len) {
    std::optional<usb::Request<>> req = completed_reads_queue_.pop();

    if (!req) {
      break;
    }

    size_t to_copy = req->request()->response.actual - offset - FTDI_STATUS_SIZE;

    if ((to_copy + bytes_copied) > len) {
      to_copy = len - bytes_copied;
    }

    size_t result = req->CopyFrom(&buffer[bytes_copied], to_copy, offset + FTDI_STATUS_SIZE);
    ZX_ASSERT(result == to_copy);
    bytes_copied = bytes_copied + to_copy;

    // If we aren't reading the whole request then put it in the front of the queue
    // and return.
    if ((to_copy + offset + FTDI_STATUS_SIZE) < req->request()->response.actual) {
      offset = offset + to_copy;
      completed_reads_queue_.push_next(*std::move(req));
      break;
    }

    // Requeue the read request.
    usb_client_.RequestQueue(req->take(), &complete);
    offset = 0;
  }

  CheckStateLocked();

  read_offset_ = offset;
  *actual = bytes_copied;

  lock.release();
  NotifyCallback();

  return ZX_OK;
}

zx_status_t FtdiDevice::SetBaudrate(uint32_t baudrate) {
  uint16_t whole, fraction, value, index;
  zx_status_t status;

  switch (ftditype_) {
    case kFtdiTypeR:
    case kFtdiType2232c:
    case kFtdiTypeBm:
      CalcDividers(&baudrate, kFtdiCClk, 16, &whole, &fraction);
      baudrate_ = baudrate;
      break;
    default:
      return ZX_ERR_INVALID_ARGS;
  }
  value = static_cast<uint16_t>((whole & 0x3fff) | (fraction << 14));
  index = static_cast<uint16_t>(fraction >> 2);
  status = usb_client_.ControlOut(USB_DIR_OUT | USB_TYPE_VENDOR | USB_RECIP_DEVICE,
                                  kFtdiSioSetBaudrate, value, index, ZX_TIME_INFINITE, NULL, 0);
  if (status == ZX_OK) {
    baudrate_ = baudrate;
  }
  return status;
}

zx_status_t FtdiDevice::Reset() {
  if (!usb_client_.is_valid()) {
    return ZX_ERR_INVALID_ARGS;
  }
  return usb_client_.ControlOut(USB_DIR_OUT | USB_TYPE_VENDOR | USB_RECIP_DEVICE,
                                kFtdiSioResetRequest, kFtdiSioReset, 0, ZX_TIME_INFINITE, NULL, 0);
}

zx_status_t FtdiDevice::SetBitMode(uint8_t line_mask, uint8_t mode) {
  uint16_t val = static_cast<uint16_t>(line_mask | (mode << 8));
  zx_status_t status =
      usb_client_.ControlOut(USB_DIR_OUT | USB_TYPE_VENDOR | USB_RECIP_DEVICE, kFtdiSioSetBitmode,
                             val, 0, ZX_TIME_INFINITE, NULL, 0);
  if (status != ZX_OK) {
    zxlogf(ERROR, "FTDI set bitmode failed with %d", status);
    return status;
  }

  return status;
}

zx_status_t FtdiDevice::SerialImplConfig(uint32_t baudrate, uint32_t flags) {
  if (baudrate != baudrate_) {
    return SetBaudrate(baudrate);
  }

  return ZX_OK;
}

zx_status_t FtdiDevice::SerialImplGetInfo(serial_port_info_t* info) {
  memcpy(info, &serial_port_info_, sizeof(*info));
  return ZX_OK;
}

zx_status_t FtdiDevice::SerialImplEnable(bool enable) {
  enabled_ = enable;
  return ZX_OK;
}

zx_status_t FtdiDevice::SerialImplSetNotifyCallback(const serial_notify_t* cb) {
  if (enabled_) {
    return ZX_ERR_BAD_STATE;
  }

  notify_cb_ = *cb;

  fbl::AutoLock lock(&mutex_);

  CheckStateLocked();

  lock.release();
  NotifyCallback();

  return ZX_OK;
}

FtdiDevice::~FtdiDevice() {}

void FtdiDevice::DdkUnbind(ddk::UnbindTxn txn) {
  cancel_thread_ = std::thread([this, unbind_txn = std::move(txn)]() mutable {
    usb_client_.CancelAll(bulk_in_addr_);
    usb_client_.CancelAll(bulk_out_addr_);
    unbind_txn.Reply();
  });
}

void FtdiDevice::DdkRelease() {
  cancel_thread_.join();
  delete this;
}

void FtdiDevice::CreateI2C(::fuchsia_hardware_ftdi::wire::I2cBusLayout layout,
                           ::fuchsia_hardware_ftdi::wire::I2cDevice device,
                           CreateI2CCompleter::Sync& completer) {
  // Set the chip to run in MPSSE mode.
  zx_status_t status = this->SetBitMode(0, 0);
  if (status != ZX_OK) {
    zxlogf(ERROR, "FTDI: setting bitmode 0 failed");
    return;
  }
  status = this->SetBitMode(0, 2);
  if (status != ZX_OK) {
    zxlogf(ERROR, "FTDI: setting bitmode 2 failed");
    return;
  }

  ftdi_mpsse::FtdiI2c::Create(this->zxdev(), &layout, &device);
}

zx_status_t FtdiDevice::DdkMessage(fidl_incoming_msg_t* msg, fidl_txn_t* txn) {
  DdkTransaction transaction(txn);
  ::fuchsia_hardware_ftdi::Device::Dispatch(this, msg, &transaction);
  return transaction.Status();
}

zx_status_t ftdi_bind_fail(zx_status_t status) {
  zxlogf(ERROR, "ftdi_bind failed: %d", status);
  return status;
}

zx_status_t FtdiDevice::Bind() {
  zx_status_t status = ZX_OK;

  if (!usb_client_.is_valid()) {
    return ZX_ERR_NOT_SUPPORTED;
  }

  fbl::AutoLock lock(&mutex_);

  // Find our endpoints.
  std::optional<usb::InterfaceList> usb_interface_list;
  status = usb::InterfaceList::Create(usb_client_, true, &usb_interface_list);
  if (status != ZX_OK) {
    return status;
  }

  uint8_t bulk_in_addr = 0;
  uint8_t bulk_out_addr = 0;

  for (auto& interface : *usb_interface_list) {
    for (auto ep_itr : interface.GetEndpointList()) {
      if (usb_ep_direction(&ep_itr.descriptor) == USB_ENDPOINT_OUT) {
        if (usb_ep_type(&ep_itr.descriptor) == USB_ENDPOINT_BULK) {
          bulk_out_addr = ep_itr.descriptor.bEndpointAddress;
        }
      } else {
        if (usb_ep_type(&ep_itr.descriptor) == USB_ENDPOINT_BULK) {
          bulk_in_addr = ep_itr.descriptor.bEndpointAddress;
        }
      }
    }
  }

  if (!bulk_in_addr || !bulk_out_addr) {
    zxlogf(ERROR, "FTDI: could not find all endpoints");
    return ZX_ERR_NOT_SUPPORTED;
  }

  ftditype_ = kFtdiTypeR;

  parent_req_size_ = usb_client_.GetRequestSize();
  for (int i = 0; i < READ_REQ_COUNT; i++) {
    std::optional<usb::Request<>> req;
    status = usb::Request<>::Alloc(&req, USB_BUF_SIZE, bulk_in_addr, parent_req_size_);
    if (status != ZX_OK) {
      zxlogf(ERROR, "FTDI allocating reads failed %d", status);
      return FtdiBindFail(status);
    }
    free_read_queue_.push(*std::move(req));
  }
  for (int i = 0; i < WRITE_REQ_COUNT; i++) {
    std::optional<usb::Request<>> req;
    status = usb::Request<>::Alloc(&req, USB_BUF_SIZE, bulk_out_addr, parent_req_size_);
    if (status != ZX_OK) {
      zxlogf(ERROR, "FTDI allocating writes failed %d", status);
      return FtdiBindFail(status);
    }
    free_write_queue_.push(*std::move(req));
  }

  status = Reset();
  if (status != ZX_OK) {
    zxlogf(ERROR, "FTDI reset failed %d", status);
    return FtdiBindFail(status);
  }

  status = SetBaudrate(115200);
  if (status != ZX_OK) {
    zxlogf(ERROR, "FTDI: set baudrate failed");
    return FtdiBindFail(status);
  }

  serial_port_info_.serial_class = fuchsia_hardware_serial_Class_GENERIC;

  status = DdkAdd("ftdi-uart");
  if (status != ZX_OK) {
    zxlogf(ERROR, "ftdi_uart: device_add failed");
    return FtdiBindFail(status);
  }

  usb_request_complete_t complete = {
      .callback =
          [](void* ctx, usb_request_t* request) {
            static_cast<FtdiDevice*>(ctx)->ReadComplete(request);
          },
      .ctx = this,
  };

  // Queue the read requests.
  std::optional<usb::Request<>> req;
  while ((req = free_read_queue_.pop())) {
    usb_client_.RequestQueue(req->take(), &complete);
  }

  bulk_in_addr_ = bulk_in_addr;
  bulk_out_addr_ = bulk_out_addr;

  zxlogf(INFO, "ftdi bind successful");
  return status;
}

}  // namespace ftdi_serial

namespace {

zx_status_t ftdi_bind(void* ctx, zx_device_t* device) {
  fbl::AllocChecker ac;
  auto dev = fbl::make_unique_checked<ftdi_serial::FtdiDevice>(&ac, device);
  if (!ac.check()) {
    return ZX_ERR_NO_MEMORY;
  }
  auto status = dev->Bind();
  if (status == ZX_OK) {
    // Devmgr is now in charge of the memory for dev.
    __UNUSED auto ptr = dev.release();
  }
  return status;
}

static constexpr zx_driver_ops_t ftdi_driver_ops = []() {
  zx_driver_ops_t ops = {};
  ops.version = DRIVER_OPS_VERSION;
  ops.bind = ftdi_bind;
  return ops;
}();

}  // namespace

ZIRCON_DRIVER(ftdi, ftdi_driver_ops, "zircon", "0.1");
