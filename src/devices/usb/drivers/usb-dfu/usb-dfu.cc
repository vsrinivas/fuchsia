// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "usb-dfu.h"

#include <fidl/fuchsia.mem/cpp/wire.h>
#include <fuchsia/hardware/usb/c/banjo.h>
#include <lib/ddk/debug.h>
#include <lib/ddk/device.h>
#include <lib/zx/vmo.h>
#include <zircon/hw/usb/dfu.h>

#include <algorithm>

#include <fbl/algorithm.h>
#include <fbl/alloc_checker.h>

#include "src/devices/usb/drivers/usb-dfu/usb_dfu_bind.h"

namespace {

constexpr uint32_t kReqTimeoutSecs = 1;

inline uint8_t MSB(int n) { return static_cast<uint8_t>(n >> 8); }
inline uint8_t LSB(int n) { return static_cast<uint8_t>(n & 0xFF); }

}  // namespace

namespace usb {

zx_status_t Dfu::ControlReq(uint8_t dir, uint8_t request, uint16_t value, void* data, size_t length,
                            size_t* out_length) {
  if (dir != USB_DIR_OUT && dir != USB_DIR_IN) {
    return ZX_ERR_INVALID_ARGS;
  }
  zx_status_t status;
  if (dir == USB_DIR_OUT) {
    status = usb_control_out(&usb_, USB_DIR_OUT | USB_TYPE_CLASS | USB_RECIP_INTERFACE, request,
                             value, intf_num_, ZX_SEC(kReqTimeoutSecs),
                             reinterpret_cast<uint8_t*>(data), length);
    if (status == ZX_OK && out_length) {
      *out_length = length;
    }
  } else {
    status = usb_control_in(&usb_, USB_DIR_IN | USB_TYPE_CLASS | USB_RECIP_INTERFACE, request,
                            value, intf_num_, ZX_SEC(kReqTimeoutSecs),
                            reinterpret_cast<uint8_t*>(data), length, out_length);
  }
  if (status == ZX_ERR_IO_REFUSED || status == ZX_ERR_IO_INVALID) {
    usb_reset_endpoint(&usb_, 0);
  }
  return status;
}

zx_status_t Dfu::Download(uint16_t block_num, uint8_t* buf, size_t len_to_write) {
  if (len_to_write > func_desc_.wTransferSize) {
    return ZX_ERR_INVALID_ARGS;
  }
  size_t out_len;
  zx_status_t status =
      ControlReq(USB_DIR_OUT, USB_DFU_DNLOAD, block_num, buf, len_to_write, &out_len);

  if (status != ZX_OK) {
    zxlogf(ERROR, "DNLOAD returned err %d", status);
    return status;
  } else if (out_len != len_to_write) {
    zxlogf(ERROR, "DNLOAD returned bad len, want: %lu, got: %lu", len_to_write, out_len);
    return ZX_ERR_IO;
  }
  return ZX_OK;
}

zx_status_t Dfu::GetStatus(usb_dfu_get_status_data_t* out_status) {
  size_t want_len = sizeof(*out_status);
  size_t out_len;
  zx_status_t status =
      ControlReq(USB_DIR_IN, USB_DFU_GET_STATUS, 0, out_status, want_len, &out_len);
  if (status != ZX_OK) {
    zxlogf(ERROR, "GET_STATUS returned err %d", status);
    return status;
  } else if (out_len != want_len) {
    zxlogf(ERROR, "GET_STATUS returned bad len, want: %lu, got: %lu", want_len, out_len);
    return ZX_ERR_IO;
  }
  return ZX_OK;
}

zx_status_t Dfu::ClearStatus() {
  size_t out_len;
  zx_status_t status = ControlReq(USB_DIR_OUT, USB_DFU_CLR_STATUS, 0, nullptr, 0, &out_len);
  if (status != ZX_OK) {
    zxlogf(ERROR, "CLR_STATUS returned err %d", status);
    return status;
  }
  return ZX_OK;
}

zx_status_t Dfu::GetState(uint8_t* out_state) {
  size_t want_len = sizeof(*out_state);
  size_t out_len;
  zx_status_t status = ControlReq(USB_DIR_IN, USB_DFU_GET_STATE, 0, out_state, want_len, &out_len);
  if (status != ZX_OK) {
    zxlogf(ERROR, "GET_STATE returned err %d", status);
    return status;
  } else if (out_len != want_len) {
    zxlogf(ERROR, "GET_STATE returned bad len, want: %lu, got: %lu", want_len, out_len);
    return ZX_ERR_IO;
  }
  return ZX_OK;
}

void Dfu::LoadPrebuiltFirmware(LoadPrebuiltFirmwareRequestView request,
                               LoadPrebuiltFirmwareCompleter::Sync& completer) {
  // TODO(jocelyndang): implement this.
  completer.Reply(ZX_ERR_NOT_SUPPORTED);
}

void Dfu::LoadFirmware(LoadFirmwareRequestView request, LoadFirmwareCompleter::Sync& completer) {
  if (request->firmware.size == 0) {
    completer.Reply(ZX_ERR_INVALID_ARGS);
    return;
  }
  size_t vmo_size;
  zx_status_t status = request->firmware.vmo.get_size(&vmo_size);
  if (status != ZX_OK) {
    zxlogf(ERROR, "failed to get firmware vmo size, err: %d", status);
    completer.Reply(ZX_ERR_INVALID_ARGS);
    return;
  }
  if (vmo_size < request->firmware.size) {
    zxlogf(ERROR, "invalid vmo, vmo size was %lu, fw size was %lu", vmo_size,
           request->firmware.size);
    completer.Reply(ZX_ERR_INVALID_ARGS);
    return;
  }

  // We need to be in the DFU Idle state.
  uint8_t state;
  status = GetState(&state);
  if (status != ZX_OK) {
    completer.Reply(status);
    return;
  }
  switch (state) {
    case USB_DFU_STATE_DFU_IDLE:
      break;
    case USB_DFU_STATE_DFU_ERROR:
      // We can get back to the DFU Idle state by clearing the error status.
      // USB DFU Spec Rev, 1.1, Table A.2.11.
      zxlogf(ERROR, "device is in dfuERROR state, trying to clear error status...");
      status = ClearStatus();
      if (status != ZX_OK) {
        zxlogf(ERROR, "could not clear error status, got err: %d", status);
        completer.Reply(status);
        return;
      }
      break;
    default:
      // TODO(jocelyndang): handle more states.
      zxlogf(ERROR, "device is in an unexpected state: %u", state);
      completer.Reply(ZX_ERR_BAD_STATE);
      return;
  }

  // Write the firmware to the device.
  // We just need to slice the firmware image into N pieces and call the USB_DFU_DNLOAD command.
  size_t vmo_offset = 0;
  // The block number is incremented per transfer.
  uint16_t block_num = 0;
  uint8_t write_buf[func_desc_.wTransferSize];

  size_t len_to_write;
  do {
    len_to_write = std::min(request->firmware.size - vmo_offset,
                            static_cast<size_t>(func_desc_.wTransferSize));
    zxlogf(DEBUG, "fetching block %u, offset %lu len %lu", block_num, vmo_offset, len_to_write);
    zx_status_t status = request->firmware.vmo.read(write_buf, vmo_offset, len_to_write);
    if (status != ZX_OK) {
      completer.Reply(status);
      return;
    }
    status = Download(block_num, write_buf, len_to_write);
    if (status != ZX_OK) {
      completer.Reply(status);
      return;
    }
    usb_dfu_get_status_data_t dfu_status;
    status = GetStatus(&dfu_status);
    if (status != ZX_OK) {
      completer.Reply(status);
      return;
    }
    if (dfu_status.bStatus != USB_DFU_STATUS_OK) {
      zxlogf(ERROR, "bad status %u", dfu_status.bStatus);
      completer.Reply(ZX_ERR_IO);
      return;
    }
    // The device expects the block number to wrap around to zero, so no need to bounds check.
    block_num++;
    vmo_offset += len_to_write;
  } while (len_to_write != 0);  // The device expects a zero length transfer to signify the end.

  completer.Reply(usb_reset_device(&usb_));
}

zx_status_t Dfu::Bind() {
  zxlogf(DEBUG, "adding DFU, interface %x, v%x.%x", intf_num_, MSB(func_desc_.bcdDFUVersion),
         LSB(func_desc_.bcdDFUVersion));
  zx_status_t status = DdkAdd("usb-dfu", DEVICE_ADD_NON_BINDABLE);
  if (status != ZX_OK) {
    return status;
  }
  return ZX_OK;
}

// static
zx_status_t Dfu::Create(zx_device_t* parent) {
  usb_protocol_t usb;
  zx_status_t status = device_get_protocol(parent, ZX_PROTOCOL_USB, &usb);
  if (status != ZX_OK) {
    return status;
  }
  usb_desc_iter_t iter;
  status = usb_desc_iter_init(&usb, &iter);
  if (status != ZX_OK) {
    return status;
  }
  usb_interface_descriptor_t* intf = usb_desc_iter_next_interface(&iter, true);
  if (!intf) {
    usb_desc_iter_release(&iter);
    return ZX_ERR_NOT_SUPPORTED;
  }
  uint8_t intf_num = intf->b_interface_number;

  // Look for the DFU Functional Descriptor.
  usb_dfu_func_desc_t func_desc = {};
  usb_descriptor_header_t* header;
  while ((header = usb_desc_iter_peek(&iter)) != nullptr) {
    if (header->b_descriptor_type == USB_DFU_CS_FUNCTIONAL) {
      if (header->b_length < sizeof(func_desc)) {
        zxlogf(ERROR, "DFU func desc should be at least %lu long, got %u", sizeof(func_desc),
               header->b_length);
      } else {
        usb_dfu_func_desc_t* desc =
            (usb_dfu_func_desc_t*)usb_desc_iter_get_structure(&iter, sizeof(func_desc));
        if (desc == nullptr) {
          zxlogf(ERROR, "DFU func desc invalid");
        } else {
          func_desc = *desc;
          zxlogf(DEBUG, "DFU func desc bmAttributes %u wDetachTimeOut %u wTransferSize %u",
                 func_desc.bmAttributes, func_desc.wDetachTimeOut, func_desc.wTransferSize);
          break;
        }
      }
    }
    usb_desc_iter_advance(&iter);
  }
  usb_desc_iter_release(&iter);

  if (func_desc.bLength == 0) {
    zxlogf(ERROR, "could not find any valid DFU functional descriptor");
    return ZX_ERR_NOT_SUPPORTED;
  }

  fbl::AllocChecker ac;
  auto dev = fbl::make_unique_checked<Dfu>(&ac, parent, usb, intf_num, func_desc);
  if (!ac.check()) {
    return ZX_ERR_NO_MEMORY;
  }

  status = dev->Bind();
  if (status == ZX_OK) {
    // Intentionally leak as it is now held by DevMgr.
    __UNUSED auto ptr = dev.release();
  }
  return status;
}

zx_status_t dfu_bind(void* ctx, zx_device_t* parent) {
  zxlogf(DEBUG, "dfu_bind");
  return usb::Dfu::Create(parent);
}

static constexpr zx_driver_ops_t dfu_driver_ops = []() {
  zx_driver_ops_t ops = {};
  ops.version = DRIVER_OPS_VERSION;
  ops.bind = dfu_bind;
  return ops;
}();

}  // namespace usb

// clang-format off
ZIRCON_DRIVER(usb_dfu, usb::dfu_driver_ops, "zircon", "0.1");
// clang-format on
