// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_USB_DRIVERS_USB_TEST_FLASH_PROGRAMMER_FLASH_PROGRAMMER_H_
#define SRC_DEVICES_USB_DRIVERS_USB_TEST_FLASH_PROGRAMMER_FLASH_PROGRAMMER_H_

#include <fuchsia/hardware/usb/fwloader/c/fidl.h>
#include <lib/zx/vmo.h>

#include <ddktl/device.h>
#include <ddktl/protocol/empty-protocol.h>
#include <usb/usb.h>

namespace usb {

class FlashProgrammer;
using FlashProgrammerBase = ddk::Device<FlashProgrammer, ddk::Messageable, ddk::Unbindable>;

class FlashProgrammer : public FlashProgrammerBase,
                        public ddk::EmptyProtocol<ZX_PROTOCOL_USB_FWLOADER> {
 public:
  // Spawns device node based on parent node.
  static zx_status_t Create(zx_device_t* parent);

  // Device protocol implementation.
  zx_status_t DdkMessage(fidl_msg_t* msg, fidl_txn_t* txn);
  void DdkUnbind(ddk::UnbindTxn txn) { txn.Reply(); }
  void DdkRelease() { delete this; }

  // FIDL message implementation.
  zx_status_t LoadPrebuiltFirmware(fuchsia_hardware_usb_fwloader_PrebuiltType type);
  zx_status_t LoadFirmware(zx::vmo fw_vmo, size_t fw_size);

 private:
  explicit FlashProgrammer(zx_device_t* parent, usb_protocol_t usb)
      : FlashProgrammerBase(parent), usb_(usb) {}

  zx_status_t Bind();

  // Sends a vendor command to write the given buffer to the device I2C EEPROM.
  zx_status_t DeviceWrite(uint8_t eeprom_slave_addr, uint16_t eeprom_byte_addr, uint8_t* buf,
                          size_t len);
  // Writes the VMO starting at |vmo_offset| to a single I2C EEPROM Slave.
  zx_status_t EEPROMSlaveWrite(uint8_t eeprom_slave_addr, const zx::vmo& fw_vmo, size_t vmo_offset,
                               uint16_t len_to_write);

  usb_protocol_t usb_;
};

}  // namespace usb

#endif  // SRC_DEVICES_USB_DRIVERS_USB_TEST_FLASH_PROGRAMMER_FLASH_PROGRAMMER_H_
