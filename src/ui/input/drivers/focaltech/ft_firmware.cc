// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ft_firmware.h"

#include <lib/ddk/debug.h>

#include "ft_device.h"

namespace {

constexpr uint8_t kFlashStatusReg = 0x6a;
constexpr uint16_t kFlashEccDone = 0xf055;
constexpr uint16_t kFlashEraseDone = 0xf0aa;

constexpr uint8_t kFirmwareEccReg = 0x66;

constexpr uint8_t kBootIdReg = 0x90;
constexpr int kGetBootIdRetries = 10;
constexpr zx::duration kBootIdWaitAfterUnlock = zx::msec(12);

constexpr uint16_t kRombootId = 0x582c;

constexpr uint8_t kChipCoreReg = 0xa3;
constexpr int kGetChipCoreRetries = 6;
constexpr uint8_t kChipCoreFirmwareValid = 0x58;

constexpr uint8_t kFirmwareVersionReg = 0xa6;

constexpr uint8_t kWorkModeReg = 0xfc;
constexpr uint8_t kWorkModeSoftwareReset1 = 0xaa;
constexpr uint8_t kWorkModeSoftwareReset2 = 0x55;

constexpr uint8_t kHidToStdReg = 0xeb;
constexpr uint16_t kHidToStdValue = 0xaa09;

// Commands and parameters
constexpr uint8_t kResetCommand = 0x07;
constexpr zx::duration kResetWait = zx::msec(400);

constexpr uint8_t kFlashEraseCommand = 0x09;
constexpr uint8_t kFlashEraseAppArea = 0x0b;

constexpr uint8_t kUnlockBootCommand = 0x55;

constexpr uint8_t kStartEraseCommand = 0x61;
constexpr zx::duration kEraseWait = zx::msec(1350);

constexpr uint8_t kEccInitializationCommand = 0x64;
constexpr uint8_t kEccCalculateCommand = 0x65;

constexpr uint8_t kFirmwarePacketCommand = 0xbf;

constexpr uint8_t kSetEraseSizeCommand = 0xb0;

// Firmware download
constexpr int kFirmwareDownloadRetries = 2;

constexpr size_t kFirmwareMinSize = 0x120;
constexpr size_t kFirmwareMaxSize = 64l * 1024;
constexpr size_t kFirmwareVersionOffset = 0x10a;

constexpr size_t kMaxPacketAddress = 0x00ff'ffff;
constexpr size_t kMaxPacketSize = 128;

constexpr size_t kMaxEraseSize = 0xfffe;

constexpr zx::duration CalculateEccSleep(const size_t check_size) {
  return zx::msec(static_cast<ssize_t>(check_size) / 256);
}

constexpr uint16_t ExpectedWriteStatus(const uint32_t address, const size_t packet_size) {
  return (0x1000 + (address / packet_size)) & 0xffff;
}

}  // namespace

namespace ft {

uint8_t FtDevice::CalculateEcc(const uint8_t* const buffer, const size_t size, uint8_t initial) {
  for (size_t i = 0; i < size; i++) {
    initial ^= buffer[i];
  }
  return initial;
}

zx_status_t FtDevice::UpdateFirmwareIfNeeded(const FocaltechMetadata& metadata) {
  if (!metadata.needs_firmware) {
    return ZX_OK;
  }

  cpp20::span<const uint8_t> firmware;
  const cpp20::span<const FirmwareEntry> entries(kFirmwareEntries, kNumFirmwareEntries);
  for (const auto& entry : entries) {
    if (entry.display_vendor == metadata.display_vendor &&
        entry.ddic_version == metadata.ddic_version) {
      firmware = cpp20::span(entry.firmware_data, entry.firmware_size);
      break;
    }
  }

  if (firmware.empty()) {
    zxlogf(ERROR, "No firmware found for vendor %u DDIC %u", metadata.display_vendor,
           metadata.ddic_version);
    return ZX_OK;
  }

  if (firmware.size() < kFirmwareMinSize) {
    zxlogf(ERROR, "Firmware binary is too small: %zu", firmware.size());
    return ZX_ERR_WRONG_TYPE;
  }
  if (firmware.size() > kFirmwareMaxSize) {
    zxlogf(ERROR, "Firmware binary is too big: %zu", firmware.size());
    return ZX_ERR_WRONG_TYPE;
  }

  zx_status_t status;
  const uint8_t firmware_version = firmware[kFirmwareVersionOffset];
  for (int i = 0; i < kFirmwareDownloadRetries; i++) {
    const zx::result<bool> firmware_status = CheckFirmwareAndStartRomboot(firmware_version);
    if (firmware_status.is_error()) {
      status = firmware_status.error_value();
      Write8(kResetCommand);
      continue;
    }
    if (!firmware_status.value()) {
      return ZX_OK;
    }

    if ((status = EraseFlash(firmware.size())) != ZX_OK) {
      Write8(kResetCommand);
      continue;
    }

    if ((status = SendFirmware(firmware)) != ZX_OK) {
      Write8(kResetCommand);
      continue;
    }

    if ((status = Write8(kResetCommand)) != ZX_OK) {
      continue;
    }

    zx::nanosleep(zx::deadline_after(kResetWait));

    zxlogf(INFO, "Firmware download completed");
    return ZX_OK;
  }

  return status;
}

zx::result<bool> FtDevice::CheckFirmwareAndStartRomboot(const uint8_t firmware_version) {
  bool firmware_valid = false;
  for (int i = 0; i < kGetChipCoreRetries; i++) {
    const zx::result<uint8_t> chip_core = ReadReg8(kChipCoreReg);
    if (chip_core.is_ok() && chip_core.value() == kChipCoreFirmwareValid) {
      firmware_valid = true;
      break;
    }
    zx::nanosleep(zx::deadline_after(zx::msec(200)));
  }
  if (!firmware_valid) {
    // Firmware is invalid, the chip must already be in romboot.
    return zx::ok(true);
  }

  const zx::result<uint8_t> current_firmware_version = ReadReg8(kFirmwareVersionReg);
  if (current_firmware_version.is_ok() && current_firmware_version.value() == firmware_version) {
    // Firmware is valid and the version matches what the driver has, no need to update.
    zxlogf(INFO, "Firmware version is current, skipping download");
    return zx::ok(false);
  }
  if (current_firmware_version.is_ok()) {
    zxlogf(INFO, "Chip firmware (0x%02x) doesn't match our version (0x%02x), starting download",
           current_firmware_version.value(), firmware_version);
  } else {
    zxlogf(WARNING, "Failed to read chip firmware version, starting download");
  }

  zx_status_t status;
  if ((status = StartRomboot()) != ZX_OK) {
    return zx::error_result(status);
  }
  if ((status = WaitForRomboot()) != ZX_OK) {
    return zx::error_result(status);
  }
  return zx::ok(true);
}

zx_status_t FtDevice::StartRomboot() {
  zx_status_t status = WriteReg8(kWorkModeReg, kWorkModeSoftwareReset1);
  if (status != ZX_OK) {
    return status;
  }
  zx::nanosleep(zx::deadline_after(zx::msec(10)));

  if ((status = WriteReg8(kWorkModeReg, kWorkModeSoftwareReset2)) != ZX_OK) {
    return status;
  }
  zx::nanosleep(zx::deadline_after(zx::msec(80)));

  return ZX_OK;
}

zx_status_t FtDevice::WaitForRomboot() {
  zx::result<uint16_t> boot_id;
  for (int i = 0; i < kGetBootIdRetries; i++) {
    boot_id = GetBootId();
    if (boot_id.is_ok() && boot_id.value() == kRombootId) {
      return ZX_OK;
    }
  }

  if (boot_id.is_error()) {
    return boot_id.error_value();
  }

  if (boot_id.value() != kRombootId) {
    zxlogf(ERROR, "Timed out waiting for boot ID 0x%04x, got 0x%04x", kRombootId, boot_id.value());
    return ZX_ERR_TIMED_OUT;
  }

  return ZX_OK;
}

zx::result<uint16_t> FtDevice::GetBootId() {
  WriteReg16(kHidToStdReg, kHidToStdValue);

  zx_status_t status = Write8(kUnlockBootCommand);
  if (status != ZX_OK) {
    zxlogf(ERROR, "Failed to send unlock command: %s", zx_status_get_string(status));
    return zx::error_result(status);
  }

  zx::nanosleep(zx::deadline_after(kBootIdWaitAfterUnlock));

  return ReadReg16(kBootIdReg);
}

zx::result<bool> FtDevice::WaitForFlashStatus(const uint16_t expected_value, const int tries,
                                              const zx::duration retry_sleep) {
  zx::result<uint16_t> value;
  for (int i = 0; i < tries; i++) {
    value = ReadReg16(kFlashStatusReg);
    if (value.is_ok() && value.value() == expected_value) {
      return zx::ok(true);
    }

    zx::nanosleep(zx::deadline_after(retry_sleep));
  }

  if (value.is_error()) {
    return zx::error(value.error_value());
  }
  return zx::ok(false);
}

zx_status_t FtDevice::SendFirmwarePacket(const uint32_t address, const uint8_t* buffer,
                                         const size_t size) {
  constexpr size_t kPacketHeaderSize = 1 + 3 + 2;  // command + address + length

  if (address > kMaxPacketAddress) {
    return ZX_ERR_INVALID_ARGS;
  }
  if (size > kMaxPacketSize) {
    return ZX_ERR_INVALID_ARGS;
  }

  uint8_t packet_buffer[kPacketHeaderSize + kMaxPacketSize];
  packet_buffer[0] = kFirmwarePacketCommand;
  packet_buffer[1] = static_cast<uint8_t>((address >> 16) & 0xff);
  packet_buffer[2] = static_cast<uint8_t>((address >> 8) & 0xff);
  packet_buffer[3] = static_cast<uint8_t>(address & 0xff);
  packet_buffer[4] = static_cast<uint8_t>((size >> 8) & 0xff);
  packet_buffer[5] = static_cast<uint8_t>(size & 0xff);
  memcpy(packet_buffer + kPacketHeaderSize, buffer, size);

  zx_status_t status = i2c_.WriteSync(packet_buffer, kPacketHeaderSize + size);
  if (status != ZX_OK) {
    zxlogf(ERROR, "Failed to write %zu bytes to 0x%06x: %s", size, address,
           zx_status_get_string(status));
    return status;
  }

  return ZX_OK;
}

zx_status_t FtDevice::EraseFlash(const size_t size) {
  zx_status_t status = WriteReg8(kFlashEraseCommand, kFlashEraseAppArea);
  if (status != ZX_OK) {
    return status;
  }

  uint8_t erase_size_buffer[4];
  erase_size_buffer[0] = kSetEraseSizeCommand;
  erase_size_buffer[1] = (size >> 16) & 0xff;
  erase_size_buffer[2] = (size >> 8) & 0xff;
  erase_size_buffer[3] = size & 0xff;
  if ((status = i2c_.WriteSync(erase_size_buffer, sizeof(erase_size_buffer))) != ZX_OK) {
    zxlogf(ERROR, "Failed to write erase size: %s", zx_status_get_string(status));
    return status;
  }

  if ((status = Write8(kStartEraseCommand)) != ZX_OK) {
    return status;
  }

  zx::nanosleep(zx::deadline_after(kEraseWait));

  const zx::result<bool> erase_done = WaitForFlashStatus(kFlashEraseDone, 50, zx::msec(400));
  if (erase_done.is_error()) {
    return erase_done.error_value();
  }
  if (!erase_done.value()) {
    zxlogf(ERROR, "Timed out waiting for flash erase");
    return ZX_ERR_TIMED_OUT;
  }

  return ZX_OK;
}

zx_status_t FtDevice::SendFirmware(cpp20::span<const uint8_t> firmware) {
  const size_t firmware_size = firmware.size();

  uint32_t address = 0;
  uint8_t expected_ecc = 0;
  zx_status_t status;
  while (!firmware.empty()) {
    const size_t send_size = std::min(kMaxPacketSize, firmware.size());
    if ((status = SendFirmwarePacket(address, firmware.data(), send_size)) != ZX_OK) {
      return status;
    }

    zx::nanosleep(zx::deadline_after(zx::msec(1)));

    const uint16_t expected_status = ExpectedWriteStatus(address, send_size);
    const zx::result<bool> write_done = WaitForFlashStatus(expected_status, 100, zx::msec(1));
    if (write_done.is_error()) {
      return write_done.error_value();
    }
    if (!write_done.value()) {
      zxlogf(WARNING, "Timed out waiting for correct flash write status");
    }

    expected_ecc = CalculateEcc(firmware.data(), send_size, expected_ecc);
    firmware = firmware.subspan(send_size);
    address += send_size;
  }

  return CheckFirmwareEcc(firmware_size, expected_ecc);
}

zx_status_t FtDevice::CheckFirmwareEcc(const size_t size, const uint8_t expected_ecc) {
  zx_status_t status = Write8(kEccInitializationCommand);
  if (status != ZX_OK) {
    return status;
  }

  size_t address = 0;
  for (size_t bytes_remaining = size; bytes_remaining > 0;) {
    const size_t check_size = std::min<size_t>(kMaxEraseSize, bytes_remaining);

    const uint8_t check_buffer[] = {
        kEccCalculateCommand,
        static_cast<uint8_t>((address >> 16) & 0xff),
        static_cast<uint8_t>((address >> 8) & 0xff),
        static_cast<uint8_t>(address & 0xff),
        static_cast<uint8_t>((check_size >> 8) & 0xff),
        static_cast<uint8_t>(check_size & 0xff),
    };
    if ((status = i2c_.WriteSync(check_buffer, sizeof(check_buffer))) != ZX_OK) {
      zxlogf(ERROR, "Failed to send ECC calculate command: %s", zx_status_get_string(status));
      return status;
    }

    zx::nanosleep(zx::deadline_after(CalculateEccSleep(check_size)));

    const zx::result<bool> ecc_done = WaitForFlashStatus(kFlashEccDone, 10, zx::msec(50));
    if (ecc_done.is_error()) {
      return ecc_done.error_value();
    }
    if (!ecc_done.value()) {
      zxlogf(ERROR, "Timed out waiting for ECC calculation");
      return ZX_ERR_TIMED_OUT;
    }

    bytes_remaining -= check_size;
    address += check_size;
  }

  const zx::result<uint8_t> ecc = ReadReg8(kFirmwareEccReg);
  if (ecc.is_error()) {
    return ecc.error_value();
  }

  if (ecc.value() != expected_ecc) {
    zxlogf(ERROR, "Firmware ECC mismatch, got 0x%02x, expected 0x%02x", ecc.value(), expected_ecc);
    return ZX_ERR_IO_DATA_LOSS;
  }

  return ZX_OK;
}

zx::result<uint8_t> FtDevice::ReadReg8(const uint8_t address) {
  uint8_t value = 0;
  zx_status_t status = i2c_.ReadSync(address, &value, sizeof(value));
  if (status != ZX_OK) {
    zxlogf(ERROR, "Failed to read from 0x%02x: %s", address, zx_status_get_string(status));
    return zx::error_result(status);
  }

  return zx::ok(value);
}

zx::result<uint16_t> FtDevice::ReadReg16(const uint8_t address) {
  uint8_t buffer[2];
  zx_status_t status = i2c_.ReadSync(address, buffer, sizeof(buffer));
  if (status != ZX_OK) {
    zxlogf(ERROR, "Failed to read from 0x%02x: %s", address, zx_status_get_string(status));
    return zx::error_result(status);
  }

  return zx::ok(static_cast<uint16_t>((buffer[0] << 8) | buffer[1]));
}

zx_status_t FtDevice::Write8(const uint8_t value) {
  zx_status_t status = i2c_.WriteSync(&value, sizeof(value));
  if (status != ZX_OK) {
    zxlogf(ERROR, "Failed to write 0x%02x: %s", value, zx_status_get_string(status));
  }
  return status;
}

zx_status_t FtDevice::WriteReg8(const uint8_t address, const uint8_t value) {
  const uint8_t buffer[] = {address, value};
  zx_status_t status = i2c_.WriteSync(buffer, sizeof(buffer));
  if (status != ZX_OK) {
    zxlogf(ERROR, "Failed to write 0x%02x to 0x%02x: %s", value, address,
           zx_status_get_string(status));
  }
  return status;
}

zx_status_t FtDevice::WriteReg16(const uint8_t address, const uint16_t value) {
  const uint8_t buffer[] = {
      address,
      static_cast<uint8_t>((value >> 8) & 0xff),
      static_cast<uint8_t>(value & 0xff),
  };
  zx_status_t status = i2c_.WriteSync(buffer, sizeof(buffer));
  if (status != ZX_OK) {
    zxlogf(ERROR, "Failed to write 0x%04x to 0x%02x: %s", value, address,
           zx_status_get_string(status));
  }
  return status;
}

}  // namespace ft
