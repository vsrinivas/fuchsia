// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "fx3.h"

#include <fuchsia/hardware/usb/fwloader/c/fidl.h>
#include <fuchsia/mem/c/fidl.h>
#include <stdlib.h>
#include <string.h>
#include <zircon/assert.h>
#include <zircon/hw/usb.h>

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/device.h>
#include <usb/usb.h>

#define FLASH_FIRMWARE_PATH "cyfxflashprog.img"
#define TESTER_FIRMWARE_PATH "fx3.img"

// The header contains the 2 byte "CY" signature, and 2 byte image metadata.
#define IMAGE_HEADER_SIZE 4

#define VENDOR_REQ_MAX_SIZE 4096
#define VENDOR_REQ_TIMEOUT_SECS 1

#define LSW(x) ((x)&0xffff)
#define MSW(x) ((x) >> 16)
#define MIN(a, b) (((a) < (b)) ? (a) : (b))

typedef struct {
  zx_device_t* zxdev;
  usb_protocol_t usb;
} fx3_t;

static zx_status_t fx3_write(fx3_t* fx3, uint8_t* buf, size_t len, uint32_t addr) {
  if (len > VENDOR_REQ_MAX_SIZE) {
    return ZX_ERR_INVALID_ARGS;
  }
  zx_status_t status;
  status = usb_control_out(&fx3->usb, USB_DIR_OUT | USB_TYPE_VENDOR | USB_RECIP_DEVICE,
                           FX3_REQ_FIRMWARE_TRANSFER, LSW(addr), MSW(addr),
                           ZX_SEC(VENDOR_REQ_TIMEOUT_SECS), buf, len);
  if (status != ZX_OK) {
    return status;
  }
  return ZX_OK;
}

// Jumps to the given address on FX3 System RAM.
static zx_status_t fx3_program_entry(fx3_t* fx3, uint32_t ram_addr) {
  return fx3_write(fx3, NULL, 0, ram_addr);
}

static zx_status_t fx3_validate_image_header(fx3_t* fx3, zx_handle_t fw_vmo) {
  uint8_t header[IMAGE_HEADER_SIZE];
  zx_status_t status = zx_vmo_read(fw_vmo, &header, 0, IMAGE_HEADER_SIZE);
  if (status != ZX_OK) {
    return status;
  }
  if (header[0] != 'C' || header[1] != 'Y') {
    return ZX_ERR_BAD_STATE;
  }
  zxlogf(TRACE, "image header: ctl 0x%02x type 0x%02x\n", header[2], header[3]);
  return ZX_OK;
}

// Writes the section data at the given device RAM address.
// Returns ZX_OK if successful, and increments checksum with the section checksum.
static zx_status_t fx3_write_section(fx3_t* fx3, zx_handle_t fw_vmo, size_t offset, size_t len,
                                     uint32_t ram_addr, uint32_t* checksum) {
  uint8_t write_buf[VENDOR_REQ_MAX_SIZE];

  while (len > 0) {
    size_t len_to_write = MIN(len, VENDOR_REQ_MAX_SIZE);
    ZX_DEBUG_ASSERT(len_to_write % 4 == 0);
    zx_status_t status = zx_vmo_read(fw_vmo, write_buf, offset, len_to_write);
    if (status != ZX_OK) {
      return status;
    }
    status = fx3_write(fx3, write_buf, len_to_write, ram_addr);
    if (status != ZX_OK) {
      return status;
    }
    uint32_t* image_data = (uint32_t*)write_buf;
    for (uint32_t i = 0; i < len_to_write / 4; ++i) {
      *checksum += image_data[i];
    }
    len -= len_to_write;
    offset += len_to_write;
    ram_addr += len_to_write;
  }
  return ZX_OK;
}

// Writes the firmware to the device RAM and boots it.
static zx_status_t fx3_load_firmware(fx3_t* fx3, zx_handle_t fw_vmo, size_t fw_size) {
  size_t vmo_size;
  zx_status_t status = zx_vmo_get_size(fw_vmo, &vmo_size);
  if (status != ZX_OK) {
    zxlogf(ERROR, "failed to get firmware vmo size, err: %d\n", status);
    return ZX_ERR_INVALID_ARGS;
  }
  if (vmo_size < fw_size) {
    zxlogf(ERROR, "invalid vmo, vmo size was %lu, fw size was %lu\n", vmo_size, fw_size);
    return ZX_ERR_INVALID_ARGS;
  }
  // The fwloader expects the firmware image file to be in the format shown in
  // EZ-USB/FX3 Boot Options, Table 14.
  status = fx3_validate_image_header(fx3, fw_vmo);
  if (status != ZX_OK) {
    zxlogf(ERROR, "invalid firmware image header, err: %d\n", status);
    return status;
  }

  size_t offset = IMAGE_HEADER_SIZE;
  uint32_t checksum = 0;
  // Section header fields.
  uint32_t len_dwords = 0;
  uint32_t ram_addr = 0;
  while (offset < fw_size) {
    // Read the section header, containing the section length in long words, and ram address.
    status = zx_vmo_read(fw_vmo, &len_dwords, offset, sizeof(len_dwords));
    if (status != ZX_OK) {
      return status;
    }
    offset += sizeof(len_dwords);
    status = zx_vmo_read(fw_vmo, &ram_addr, offset, sizeof(ram_addr));
    if (status != ZX_OK) {
      return status;
    }
    offset += sizeof(ram_addr);
    zxlogf(TRACE, "section len %u B ram addr 0x%x\n", len_dwords * 4, ram_addr);

    if (len_dwords == 0) {
      // Reached termination of image.
      break;
    }
    status = fx3_write_section(fx3, fw_vmo, offset, len_dwords * 4, ram_addr, &checksum);
    if (status != ZX_OK) {
      zxlogf(ERROR, "fx3_write_section failed, err: %d\n", status);
      return status;
    }
    offset += (len_dwords * 4);
  }
  if (len_dwords != 0) {
    // Didn't get termination of image indicator.
    return ZX_ERR_BAD_STATE;
  }
  uint32_t expected_checksum;
  status = zx_vmo_read(fw_vmo, &expected_checksum, offset, sizeof(expected_checksum));
  if (status != ZX_OK) {
    zxlogf(ERROR, "could not read expected checksum, err: %d\n", status);
    return status;
  }
  if (checksum != expected_checksum) {
    zxlogf(ERROR, "got bad checksum %u, want %u\n", checksum, expected_checksum);
    return ZX_ERR_BAD_STATE;
  }
  status = fx3_program_entry(fx3, ram_addr);
  if (status == ZX_OK) {
    return ZX_OK;
  } else if (status == ZX_ERR_IO_REFUSED) {
    // When using the second stage bootloader, the control request may send an error code
    // back after we jump to the program entry.
    zxlogf(TRACE, "fx3_program_entry got expected err: %d\n", status);
    return ZX_OK;
  } else {
    zxlogf(ERROR, "fx3_program_entry got unexpected err: %d\n", status);
    return status;
  }
}

static zx_status_t fidl_LoadPrebuiltFirmware(void* ctx,
                                             fuchsia_hardware_usb_fwloader_PrebuiltType type,
                                             fidl_txn_t* txn) {
  fx3_t* fx3 = ctx;

  const char* fw_path = NULL;
  switch (type) {
    case fuchsia_hardware_usb_fwloader_PrebuiltType_FLASH:
      fw_path = FLASH_FIRMWARE_PATH;
      break;
    case fuchsia_hardware_usb_fwloader_PrebuiltType_TESTER:
      fw_path = TESTER_FIRMWARE_PATH;
      break;
    default:
      zxlogf(ERROR, "unsupported firmware type: %u\n", type);
      return ZX_ERR_NOT_SUPPORTED;
  }

  zx_handle_t fw_vmo;
  size_t fw_size;
  zx_status_t status = load_firmware(fx3->zxdev, fw_path, &fw_vmo, &fw_size);
  if (status != ZX_OK) {
    zxlogf(ERROR,
           "failed to load firmware at path "
           "%s"
           ", err: %d\n",
           fw_path, status);
    return fuchsia_hardware_usb_fwloader_DeviceLoadPrebuiltFirmware_reply(txn, status);
  }
  status = fx3_load_firmware(fx3, fw_vmo, fw_size);
  zx_handle_close(fw_vmo);
  return fuchsia_hardware_usb_fwloader_DeviceLoadPrebuiltFirmware_reply(txn, status);
}

static zx_status_t fidl_LoadFirmware(void* ctx, const fuchsia_mem_Buffer* firmware,
                                     fidl_txn_t* txn) {
  fx3_t* fx3 = ctx;
  zx_status_t status = fx3_load_firmware(fx3, firmware->vmo, firmware->size);
  zx_handle_close(firmware->vmo);
  return fuchsia_hardware_usb_fwloader_DeviceLoadFirmware_reply(txn, status);
}

static fuchsia_hardware_usb_fwloader_Device_ops_t fidl_ops = {
    .LoadPrebuiltFirmware = fidl_LoadPrebuiltFirmware,
    .LoadFirmware = fidl_LoadFirmware,
};

static zx_status_t fx3_message(void* ctx, fidl_msg_t* msg, fidl_txn_t* txn) {
  return fuchsia_hardware_usb_fwloader_Device_dispatch(ctx, txn, msg, &fidl_ops);
}

static void fx3_free(fx3_t* ctx) { free(ctx); }

static void fx3_unbind(void* ctx) {
  zxlogf(INFO, "fx3_unbind\n");
  fx3_t* fx3 = ctx;

  device_unbind_reply(fx3->zxdev);
}

static void fx3_release(void* ctx) {
  fx3_t* fx3 = ctx;
  fx3_free(fx3);
}

static zx_protocol_device_t fx3_device_protocol = {
    .version = DEVICE_OPS_VERSION,
    .message = fx3_message,
    .unbind = fx3_unbind,
    .release = fx3_release,
};

static zx_status_t fx3_bind(void* ctx, zx_device_t* device) {
  zxlogf(TRACE, "fx3_bind\n");

  fx3_t* fx3 = calloc(1, sizeof(fx3_t));
  if (!fx3) {
    return ZX_ERR_NO_MEMORY;
  }
  zx_status_t result = device_get_protocol(device, ZX_PROTOCOL_USB, &fx3->usb);
  if (result != ZX_OK) {
    fx3_free(fx3);
    return result;
  }

  device_add_args_t args = {
      .version = DEVICE_ADD_ARGS_VERSION,
      .name = "fx3",
      .ctx = fx3,
      .ops = &fx3_device_protocol,
      .flags = DEVICE_ADD_NON_BINDABLE,
      .proto_id = ZX_PROTOCOL_USB_FWLOADER,
  };

  zx_status_t status = device_add(device, &args, &fx3->zxdev);
  if (status != ZX_OK) {
    fx3_free(fx3);
    return status;
  }
  return ZX_OK;
}

static zx_driver_ops_t fx3_driver_ops = {
    .version = DRIVER_OPS_VERSION,
    .bind = fx3_bind,
};

ZIRCON_DRIVER_BEGIN(fx3, fx3_driver_ops, "zircon", "0.1", 4)
BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_USB), BI_ABORT_IF(NE, BIND_USB_VID, CYPRESS_VID),
    BI_MATCH_IF(EQ, BIND_USB_PID, FX3_DEFAULT_BOOTLOADER_PID),
    BI_MATCH_IF(EQ, BIND_USB_PID, FX3_SECOND_STAGE_BOOTLOADER_PID), ZIRCON_DRIVER_END(fx3)
