// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "flash-programmer.h"

#include <fuchsia/hardware/usb/fwloader/c/fidl.h>
#include <fuchsia/mem/c/fidl.h>
#include <lib/zx/vmo.h>

#include <limits>
#include <memory>

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/device.h>
#include <fbl/algorithm.h>
#include <fbl/alloc_checker.h>

#include "flash-programmer-hw.h"

namespace {

constexpr char kBootFirmwarePath[] = "Fx3BootAppGcc.img";

// The expected image format is detailed in EZ-USB/FX3 Boot Options, Table 19.
constexpr uint32_t kImageHeaderSize = 4;

constexpr uint32_t kKB = 1024;
// The I2C EEPROM size is stored in the firmware image header as a number from 0 to 7,
// which is the index for this lookup table.
constexpr uint32_t KNumEepromSizes = 8;
constexpr uint32_t kEepromSizeLUT[KNumEepromSizes] = {
    0,  // Reserved
    0,  // Reserved
    4 * kKB, 8 * kKB, 16 * kKB, 32 * kKB, 64 * kKB, 128 * kKB,
};

// The maximum number of addressable EEPROMs.
constexpr uint32_t kMaxNumEeproms = 8;

// Vendor request write sizes must be a multiple of this.
constexpr uint16_t kVendorReqSizeAlignment = 64;
constexpr uint16_t kVendorReqMaxSize = 4096;
constexpr uint32_t kReqTimeoutSecs = 1;

struct ImageHeader {
  uint8_t signature[2];
  uint8_t image_ctl;
  uint8_t image_type;
} __PACKED;
static_assert(sizeof(ImageHeader) == kImageHeaderSize, "");

zx_status_t ParseImageHeader(const zx::vmo& fw_vmo, uint32_t* out_i2c_size) {
  // The header layout can be found in EZ-USB/FX3 Boot Options, Table 19.
  ImageHeader image_header;
  zx_status_t status = fw_vmo.read(&image_header, 0, sizeof(image_header));
  if (status != ZX_OK) {
    return status;
  }
  if (image_header.signature[0] != 'C' || image_header.signature[1] != 'Y') {
    return ZX_ERR_BAD_STATE;
  }

  // I2C size is stored in bits 1 to 3 of image_ctl.
  uint32_t idx = (image_header.image_ctl >> 1) & 0x7;
  ZX_DEBUG_ASSERT(idx < KNumEepromSizes);
  *out_i2c_size = kEepromSizeLUT[idx];

  zxlogf(TRACE, "image header: ctl 0x%02x type 0x%02x i2c eeprom size %u", image_header.image_ctl,
         image_header.image_type, *out_i2c_size);
  return ZX_OK;
}

zx_status_t fidl_LoadPrebuiltFirmware(void* ctx, fuchsia_hardware_usb_fwloader_PrebuiltType type,
                                      fidl_txn_t* txn) {
  auto fp = static_cast<usb::FlashProgrammer*>(ctx);
  zx_status_t status = fp->LoadPrebuiltFirmware(type);
  return fuchsia_hardware_usb_fwloader_DeviceLoadPrebuiltFirmware_reply(txn, status);
}

zx_status_t fidl_LoadFirmware(void* ctx, const fuchsia_mem_Buffer* firmware, fidl_txn_t* txn) {
  auto fp = static_cast<usb::FlashProgrammer*>(ctx);
  zx_status_t status = fp->LoadFirmware(zx::vmo(firmware->vmo), firmware->size);
  return fuchsia_hardware_usb_fwloader_DeviceLoadFirmware_reply(txn, status);
}

fuchsia_hardware_usb_fwloader_Device_ops_t fidl_ops = {
    .LoadPrebuiltFirmware = fidl_LoadPrebuiltFirmware,
    .LoadFirmware = fidl_LoadFirmware,
};

}  // namespace

namespace usb {

zx_status_t FlashProgrammer::DeviceWrite(uint8_t eeprom_slave_addr, uint16_t eeprom_byte_addr,
                                         uint8_t* buf, size_t len_to_write) {
  if (len_to_write > kVendorReqMaxSize) {
    return ZX_ERR_INVALID_ARGS;
  }
  zx_status_t status = usb_control_out(&usb_, USB_DIR_OUT | USB_TYPE_VENDOR | USB_RECIP_DEVICE,
                                       FLASH_PROGRAMMER_WRITE, eeprom_slave_addr, eeprom_byte_addr,
                                       ZX_SEC(kReqTimeoutSecs), buf, len_to_write);
  if (status != ZX_OK) {
    zxlogf(ERROR, "usb control returned err %d", status);
    return status;
  }
  return ZX_OK;
}

zx_status_t FlashProgrammer::EEPROMSlaveWrite(uint8_t eeprom_slave_addr, const zx::vmo& fw_vmo,
                                              size_t vmo_offset, uint16_t len_to_write) {
  // We need to do the writes in up to 4K chunks.
  uint8_t write_buf[kVendorReqMaxSize];
  uint16_t eeprom_byte_addr = 0;

  size_t total_written = 0;
  while (total_written < len_to_write) {
    // The request size needs to be a multiple of kVendorReqSizeAlignment,
    // so make sure the buffer is padded with zeros.
    memset(write_buf, 0, kVendorReqMaxSize);
    size_t req_write_len =
        fbl::min(len_to_write - total_written, static_cast<size_t>(kVendorReqMaxSize));

    zx_status_t status = fw_vmo.read(write_buf, vmo_offset, req_write_len);
    if (status != ZX_OK) {
      return status;
    }
    req_write_len = fbl::round_up(req_write_len, kVendorReqSizeAlignment);
    status = DeviceWrite(eeprom_slave_addr, eeprom_byte_addr, write_buf, req_write_len);

    zxlogf(TRACE, "EEPROM [%u] write addr %u vmo offset %lu len to write %lu status %d",
           eeprom_slave_addr, eeprom_byte_addr, vmo_offset, req_write_len, status);

    if (status != ZX_OK) {
      return status;
    }
    total_written += req_write_len;
    eeprom_byte_addr = static_cast<uint16_t>(eeprom_byte_addr + req_write_len);
    vmo_offset += req_write_len;
  }
  return ZX_OK;
}

zx_status_t FlashProgrammer::LoadPrebuiltFirmware(fuchsia_hardware_usb_fwloader_PrebuiltType type) {
  const char* fw_path = nullptr;
  switch (type) {
    case fuchsia_hardware_usb_fwloader_PrebuiltType_BOOT:
      fw_path = kBootFirmwarePath;
      break;
    default:
      zxlogf(ERROR, "unsupported firmware type: %u", type);
      return ZX_ERR_NOT_SUPPORTED;
  }

  zx::vmo fw_vmo;
  size_t fw_size;
  zx_status_t status = load_firmware(zxdev(), fw_path, fw_vmo.reset_and_get_address(), &fw_size);
  if (status != ZX_OK) {
    zxlogf(ERROR,
           "failed to load firmware at path "
           "%s"
           ", err: %d\n",
           fw_path, status);
    return status;
  }
  return LoadFirmware(std::move(fw_vmo), fw_size);
}

zx_status_t FlashProgrammer::LoadFirmware(zx::vmo fw_vmo, size_t fw_size) {
  size_t vmo_size;
  zx_status_t status = fw_vmo.get_size(&vmo_size);
  if (status != ZX_OK) {
    zxlogf(ERROR, "failed to get firmware vmo size, err: %d", status);
    return ZX_ERR_INVALID_ARGS;
  }
  if (vmo_size < fw_size) {
    zxlogf(ERROR, "invalid vmo, vmo size was %lu, fw size was %lu", vmo_size, fw_size);
    return ZX_ERR_INVALID_ARGS;
  }
  uint32_t eeprom_size;
  status = ParseImageHeader(fw_vmo, &eeprom_size);
  if (status != ZX_OK) {
    zxlogf(ERROR, "invalid firmware image header, err: %d", status);
    return status;
  }
  if (eeprom_size == 0) {
    return ZX_ERR_NOT_SUPPORTED;
  }
  if (fw_size > eeprom_size * kMaxNumEeproms) {
    return ZX_ERR_OUT_OF_RANGE;
  }

  size_t vmo_offset = 0;
  uint8_t eeprom_slave_addr = 0;
  while (vmo_offset < fw_size) {
    // Write up to the EEPROM size.
    size_t len_to_write = fbl::min(fw_size - vmo_offset, static_cast<size_t>(eeprom_size));
    // TODO(jocelyndang): different handling needs to be done for 128K EEPROMs.
    if (len_to_write > std::numeric_limits<uint16_t>::max()) {
      return ZX_ERR_NOT_SUPPORTED;
    }
    status = EEPROMSlaveWrite(eeprom_slave_addr, fw_vmo, vmo_offset,
                              static_cast<uint16_t>(len_to_write));
    if (status != ZX_OK) {
      return status;
    }
    vmo_offset += len_to_write;
    eeprom_slave_addr++;
  }
  return ZX_OK;
}

zx_status_t FlashProgrammer::DdkMessage(fidl_msg_t* msg, fidl_txn_t* txn) {
  return fuchsia_hardware_usb_fwloader_Device_dispatch(this, txn, msg, &fidl_ops);
}

zx_status_t FlashProgrammer::Bind() { return DdkAdd("flash-programmer", DEVICE_ADD_NON_BINDABLE); }

// static
zx_status_t FlashProgrammer::Create(zx_device_t* parent) {
  usb_protocol_t usb;
  zx_status_t status = device_get_protocol(parent, ZX_PROTOCOL_USB, &usb);
  if (status != ZX_OK) {
    return status;
  }
  fbl::AllocChecker ac;
  std::unique_ptr<FlashProgrammer> dev(new (&ac) FlashProgrammer(parent, usb));
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

extern "C" zx_status_t flash_programmer_bind(void* ctx, zx_device_t* parent) {
  zxlogf(TRACE, "flash_programmer_bind");
  return usb::FlashProgrammer::Create(parent);
}

static constexpr zx_driver_ops_t flash_programmer_driver_ops = []() {
  zx_driver_ops_t ops = {};
  ops.version = DRIVER_OPS_VERSION;
  ops.bind = flash_programmer_bind;
  return ops;
}();

}  // namespace usb

// clang-format off
ZIRCON_DRIVER_BEGIN(flash_programmer, usb::flash_programmer_driver_ops, "zircon", "0.1", 3)
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_USB_DEVICE),
    BI_ABORT_IF(NE, BIND_USB_VID, CYPRESS_VID),
    BI_MATCH_IF(EQ, BIND_USB_PID, FLASH_PROGRAMMER_PID),
ZIRCON_DRIVER_END(flash_programmer)
    // clang-format on
