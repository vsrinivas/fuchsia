// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <endian.h>
#include <lib/ddk/debug.h>

#include "gt92xx.h"

namespace {

// Firmware file definitions
constexpr size_t kProductIdOffset = 4;
constexpr size_t kVersionIdOffset = 12;
constexpr size_t kFirmwareHeaderSize = 14;
constexpr size_t kMatchingHeaderFirmwareSize = 42 * 1024;
constexpr size_t kFirmwareSectionSize = 0x2000;
constexpr size_t kFirmwareTotalSectionSize = 4 * kFirmwareSectionSize;

constexpr size_t kDspIspSize = 0x1000;
constexpr size_t kDspSize = 0x1000;
constexpr size_t kBootSize = 0x800;
constexpr size_t kBootIspSize = 0x800;
constexpr size_t kLinkSection1Size = kFirmwareSectionSize;
constexpr size_t kLinkSection2Size = 0x1000;

// I2C interface definitions
constexpr size_t kMaxI2cAccessSize = 256;

// Copy command not relevant for DSP ISP
constexpr goodix::Gt92xxDevice::SectionInfo kDspIspSection = {0xc000, 2, 0};
constexpr goodix::Gt92xxDevice::SectionInfo kGwakeSections[] = {
    {0x9000, 3, 0xa},
    {0x9000, 3, 0xb},
    {0x9000, 3, 0xc},
    {0x9000, 3, 0xd},
};
constexpr goodix::Gt92xxDevice::SectionInfo kSs51Sections[] = {
    {0xc000, 0, 0x1},
    {0xe000, 0, 0x2},
    {0xc000, 1, 0x3},
    {0xe000, 1, 0x4},
};
constexpr goodix::Gt92xxDevice::SectionInfo kDspSection = {0x9000, 3, 0x5};
constexpr goodix::Gt92xxDevice::SectionInfo kBootSection = {0x9000, 3, 0x6};
constexpr goodix::Gt92xxDevice::SectionInfo kBootIspSection = {0x9000, 3, 0x7};
constexpr goodix::Gt92xxDevice::SectionInfo kLinkSections[] = {
    {0x9000, 3, 0x8},
    {0x9000, 3, 0x9},
};

constexpr uint8_t kEnableDspCodeDownloadCommand = 0x99;

}  // namespace

namespace goodix {

void Gt92xxDevice::LogFirmwareStatus() {
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wc99-designator"
  constexpr const char* kFirmwareStatusStrings[] = {
      [kNoFirmware] = "Skipped, no firmware supplied",
      [kInternalError] = "Failed, internal error",
      [kFirmwareInvalid] = "Failed, firmware invalid",
      [kFirmwareNotApplicable] = "Skipped, firmware not applicable to chip",
      [kChipFirmwareCurrent] = "Skipped, chip firmware current",
      [kFirmwareUpdateError] = "Failed, chip error",
      [kFirmwareUpdated] = "Succeeded",
  };
#pragma GCC diagnostic pop
  static_assert(std::size(kFirmwareStatusStrings) == kFirmwareStatusCount);

  node_ = inspector_.GetRoot().CreateChild("Chip info");

  uint8_t config_version;
  zx_status_t status = Read(GT_REG_CONFIG_DATA, &config_version, sizeof(config_version));
  if (status == ZX_OK) {
    node_.CreateByteVector("CONFIG_VERSION", {&config_version, sizeof(config_version)}, &values_);
    zxlogf(INFO, "  CONFIG_VERSION: 0x%02x", config_version);
  } else {
    node_.CreateString("CONFIG_VERSION", "error", &values_);
    zxlogf(ERROR, "  CONFIG_VERSION: error %d", status);
  }

  union {
    uint8_t fw_buffer[2];
    uint16_t fw_version;
  };
  status = Read(GT_REG_FW_VERSION, fw_buffer, sizeof(fw_buffer));
  if (status == ZX_OK) {
    const uint8_t fw_buffer_rev[] = {fw_buffer[1], fw_buffer[0]};
    node_.CreateByteVector("FW_VERSION", fw_buffer_rev, &values_);
    fw_version = le16toh(fw_version);
    zxlogf(INFO, "  FW_VERSION: 0x%04x", fw_version);
  } else {
    node_.CreateString("FW_VERSION", "error", &values_);
    zxlogf(ERROR, "  FW_VERSION: error %d", status);
  }

  node_.CreateString("Firmware update", kFirmwareStatusStrings[firmware_status_], &values_);
}

bool Gt92xxDevice::ProductIdsMatch(const uint8_t* firmware_product_id,
                                   const uint8_t* chip_product_id) {
  constexpr size_t kProductIdMinSize = 3;
  constexpr size_t kProductIdMaxSize = 4;

  size_t size;
  for (size = 0; size < kProductIdMaxSize && firmware_product_id[size] != '\0'; size++) {
    if (firmware_product_id[size] < '0' || firmware_product_id[size] > '9') {
      return false;
    }
  }
  if (size < kProductIdMinSize || firmware_product_id[size] != '\0') {
    return false;
  }

  return memcmp(firmware_product_id, chip_product_id, size) == 0;
}

zx::result<fzl::VmoMapper> Gt92xxDevice::LoadAndVerifyFirmware() {
  constexpr size_t kMinFirmwareSize = kFirmwareHeaderSize + kDspIspSize +
                                      kFirmwareTotalSectionSize + kFirmwareTotalSectionSize +
                                      kDspSize + kBootSize;

  zx::vmo firmware_vmo;
  size_t firmware_size = 0;
  zx_status_t status = load_firmware(parent(), GT9293_ASTRO_FIRMWARE_PATH,
                                     firmware_vmo.reset_and_get_address(), &firmware_size);
  if (status != ZX_OK) {
    zxlogf(WARNING, "Failed to load firmware: %d", status);
    firmware_status_ = kNoFirmware;
    return zx::error(ZX_ERR_NOT_FOUND);
  }

  if (firmware_size < kMinFirmwareSize) {
    zxlogf(ERROR, "Firmware size is %zu, expected at least %zu", firmware_size, kMinFirmwareSize);
    firmware_status_ = kFirmwareInvalid;
    return zx::error(ZX_ERR_INTERNAL);
  }
  if (firmware_size % sizeof(uint16_t) != 0) {
    zxlogf(ERROR, "Firmware size %zu is not divisible by %zu", firmware_size, sizeof(uint16_t));
    firmware_status_ = kFirmwareInvalid;
    return zx::error(ZX_ERR_INTERNAL);
  }

  fzl::VmoMapper firmware_mapper;
  if ((status = firmware_mapper.Map(firmware_vmo, 0, firmware_size, ZX_VM_PERM_READ)) != ZX_OK) {
    zxlogf(ERROR, "Failed to map firmware: %d", status);
    firmware_status_ = kInternalError;
    return zx::error(ZX_ERR_INTERNAL);
  }

  auto* firmware = reinterpret_cast<const uint8_t*>(firmware_mapper.start());

  uint16_t checksum = 0;
  for (size_t i = kFirmwareHeaderSize; i < firmware_size; i += sizeof(uint16_t)) {
    checksum += be16toh(*reinterpret_cast<const uint16_t*>(&firmware[i]));
  }

  if (checksum != 0) {
    zxlogf(ERROR, "Firmware checksum failed");
    firmware_status_ = kFirmwareInvalid;
    return zx::error(ZX_ERR_INTERNAL);
  }

  return zx::ok(std::move(firmware_mapper));
}

bool Gt92xxDevice::IsFirmwareApplicable(const fzl::VmoMapper& firmware_mapper) {
  const uint8_t* firmware = reinterpret_cast<const uint8_t*>(firmware_mapper.start());
  uint32_t firmware_hw_info;
  memcpy(&firmware_hw_info, firmware, sizeof(firmware_hw_info));
  firmware_hw_info = be32toh(firmware_hw_info);

  // 2. Read firmware message and double-check
  uint32_t hw_info;
  zx_status_t status = Read(GT_REG_HW_INFO, reinterpret_cast<uint8_t*>(&hw_info), sizeof(hw_info));
  if (status != ZX_OK) {
    return status;
  }

  uint32_t hw_info_1;
  status = Read(GT_REG_HW_INFO, reinterpret_cast<uint8_t*>(&hw_info_1), sizeof(hw_info_1));
  if (status != ZX_OK) {
    return status;
  }

  if (hw_info != hw_info_1) {
    zxlogf(ERROR, "Reads from 0x%04x returned different data", GT_REG_HW_INFO);
    return ZX_ERR_IO_DATA_INTEGRITY;
  }

  hw_info = le32toh(hw_info);

  zx::result<uint8_t> fw_message = zx::ok(0);
  for (int i = 0; i < GT_REG_FW_MESSAGE_RETRIES; i++) {
    fw_message = Read(GT_REG_FW_MESSAGE);
    if (fw_message.is_error() || fw_message.value() == GT_FIRMWARE_MAGIC) {
      break;
    }
  }
  if (fw_message.is_error()) {
    return fw_message.status_value();
  }

  const bool force_update = fw_message.value() != GT_FIRMWARE_MAGIC;

  uint8_t product_info[sizeof(uint32_t) + sizeof(uint16_t)];
  if ((status = Read(GT_REG_PRODUCT_INFO, product_info, sizeof(product_info))) != ZX_OK) {
    return status;
  }

  uint16_t version_id;
  memcpy(&version_id, product_info + sizeof(uint32_t), sizeof(version_id));
  version_id = le16toh(version_id);

  // Condition 1.b.
  size_t expected_size = firmware_hw_info;
  if (hw_info == firmware_hw_info) {
    // Condition 1.a.
    expected_size = kMatchingHeaderFirmwareSize;
  }
  if (expected_size != (firmware_mapper.size() - kFirmwareHeaderSize)) {
    zxlogf(WARNING, "Firmware size (%zu) doesn't match expected size (%zu)",
           firmware_mapper.size() - kFirmwareHeaderSize, expected_size);
    firmware_status_ = kFirmwareInvalid;
    return false;
  }

  // Condition 2
  if (force_update) {
    return true;
  }

  // Condition 3
  if (!ProductIdsMatch(firmware + kProductIdOffset, product_info)) {
    zxlogf(WARNING, "Firmware product ID doesn't match chip");
    firmware_status_ = kFirmwareNotApplicable;
    return false;
  }

  uint16_t fw_version_id;
  memcpy(&fw_version_id, firmware + kVersionIdOffset, sizeof(fw_version_id));
  fw_version_id = be16toh(fw_version_id);

  // Condition 4
  if (fw_version_id <= version_id) {
    zxlogf(INFO, "Chip firmware (0x%04x) is current, skipping download", version_id);
    firmware_status_ = kChipFirmwareCurrent;
    return false;
  }

  return true;
}

zx_status_t Gt92xxDevice::EnterUpdateMode() {
  reset_gpio_.ConfigOut(0);                        // 1. Reset output low
  zx::nanosleep(zx::deadline_after(zx::msec(2)));  // 2. Sleep 2ms

  int_gpio_.ConfigOut(0);  // 3. INT output low (assuming address isn't 0x14)
  zx::nanosleep(zx::deadline_after(zx::msec(2)));  // 4. Sleep 2ms

  reset_gpio_.ConfigOut(1);                        // 5. Reset output high
  zx::nanosleep(zx::deadline_after(zx::msec(5)));  // 6. Sleep 5ms

  zx_status_t status = HoldSs51AndDsp();  // 7. Hold SS51 and DSP, verify the result
  if (status != ZX_OK) {
    return status;
  }

  zx::result<bool> updated = Ss51AndDspHeld();
  if (updated.is_error()) {
    return updated.error_value();
  }
  if (!updated.value()) {
    zxlogf(ERROR, "Register 0x%04x didn't update", GT_REG_SW_RESET);
    return ZX_ERR_IO;
  }

  if ((status = Write(GT_REG_DSP_CONTROL, 0)) != ZX_OK) {  // 8. Enable clocks
    return status;
  }

  return ZX_OK;
}

void Gt92xxDevice::LeaveUpdateMode() {
  int_gpio_.ConfigIn(GPIO_PULL_UP);  // 1. INT input

  // General reset

  reset_gpio_.ConfigOut(0);                         // 2.1. Reset output low
  zx::nanosleep(zx::deadline_after(zx::msec(20)));  // 2.2. Sleep 20ms

  int_gpio_.ConfigOut(0);  // 2.3. INT output low (assuming address isn't 0x14)
  zx::nanosleep(zx::deadline_after(zx::msec(2)));  // 2.4. Sleep 2ms

  reset_gpio_.ConfigOut(1);                        // 2.5. Reset output high
  zx::nanosleep(zx::deadline_after(zx::msec(6)));  // 2.6. Sleep 6ms

  reset_gpio_.ConfigIn(0);                          // 2.7. Reset input
  int_gpio_.ConfigOut(0);                           // 2.8. INT output low
  zx::nanosleep(zx::deadline_after(zx::msec(50)));  // 2.9. Sleep 50ms

  int_gpio_.ConfigIn(GPIO_PULL_UP);  // 2.10. INT input

  // Device requires 50ms delay between setting INT to input and sending config (per datasheet)
  zx::nanosleep(zx::deadline_after(zx::msec(50)));
}

zx_status_t Gt92xxDevice::WritePayload(uint16_t address, cpp20::span<const uint8_t> data) {
  int retries = 0;
  while (!data.empty()) {
    const size_t send_size = std::min(kMaxI2cAccessSize, data.size());

    uint8_t buffer[sizeof(address) + kMaxI2cAccessSize];
    buffer[0] = static_cast<uint8_t>(address >> 8);
    buffer[1] = static_cast<uint8_t>(address & 0xff);
    memcpy(&buffer[sizeof(address)], data.data(), send_size);
    zx_status_t status = Write(buffer, sizeof(address) + send_size);
    if (status != ZX_OK) {
      if (++retries >= kI2cRetries) {
        zxlogf(ERROR, "Failed to write payload to 0x%04x: %d", address, status);
        return status;
      }
      continue;
    }

    // Read the frame back and verify that it matched.
    if ((status = Read(address, buffer, send_size)) != ZX_OK) {
      if (++retries >= kI2cRetries) {
        zxlogf(ERROR, "Failed to read back payload from 0x%04x: %d", address, status);
        return status;
      }
      continue;
    }

    if (memcmp(data.data(), buffer, send_size) != 0) {
      if (++retries >= kI2cRetries) {
        zxlogf(ERROR, "Data read back from 0x%04x did not match data sent", address);
        return ZX_ERR_IO_DATA_INTEGRITY;
      }
      continue;
    }

    address += send_size;
    data = data.subspan(send_size);
    retries = 0;
  }

  return ZX_OK;
}

zx_status_t Gt92xxDevice::VerifyPayload(uint16_t address, cpp20::span<const uint8_t> data) {
  while (!data.empty()) {
    const size_t send_size = std::min(kMaxI2cAccessSize, data.size());

    uint8_t buffer[kMaxI2cAccessSize];
    zx_status_t status = Read(address, buffer, send_size);
    if (status != ZX_OK) {
      zxlogf(ERROR, "Failed to read back payload from 0x%04x: %d", address, status);
      return status;
    }

    if (memcmp(data.data(), buffer, send_size) != 0) {
      zxlogf(ERROR, "Data read back from 0x%04x did not match data sent", address);
      return ZX_ERR_IO_DATA_INTEGRITY;
    }

    address += send_size;
    data = data.subspan(send_size);
  }

  return ZX_OK;
}

zx_status_t Gt92xxDevice::WaitUntilNotBusy() {
  for (;;) {
    zx::nanosleep(zx::deadline_after(zx::msec(10)));

    zx::result status = DeviceBusy();
    if (status.is_error()) {
      return status.error_value();
    }
    if (!status.value()) {
      return ZX_OK;
    }
  }
}

zx_status_t Gt92xxDevice::WriteDspIsp(cpp20::span<const uint8_t> dsp_isp) {
  zx_status_t status = DisableWdt();  // 1. Disable WDT
  if (status != ZX_OK) {
    return status;
  }

  if ((status = DisableCache()) != ZX_OK) {  // 2. Disable cache
    return status;
  }

  if ((status = HoldSs51AndDsp()) != ZX_OK) {  // 3. Hold SS51 and DSP
    return status;
  }

  if ((status = SetBootFromSram()) != ZX_OK) {  // 4. Set boot from SRAM
    return status;
  }

  if ((status = TriggerSoftwareReset()) != ZX_OK) {  // 5. Software reset
    return status;
  }

  if ((status = SetSramBank(kDspIspSection.sram_bank)) != ZX_OK) {  // 6. Select bank
    return status;
  }

  if ((status = EnableCodeAccess()) != ZX_OK) {  // 7. Enable code access
    return status;
  }

  if ((status = WritePayload(kDspIspSection.address, dsp_isp)) != ZX_OK) {  // 8. Write section
    return status;
  }

  if ((status = SetScramble()) != ZX_OK) {  // 9. Set scramble
    return status;
  }

  return ZX_OK;
}

zx_status_t Gt92xxDevice::WriteGwakeOrLinkSection(SectionInfo section_info,
                                                  cpp20::span<const uint8_t> section) {
  zx_status_t status = HoldSs51AndDsp();  // a. Hold SS51 and DSP
  if (status != ZX_OK) {
    return status;
  }

  if ((status = SetScramble()) != ZX_OK) {  // b. Set scramble
    return status;
  }

  if ((status = HoldSs51ReleaseDsp()) != ZX_OK) {  // c. Release DSP
    return status;
  }

  zx::nanosleep(zx::deadline_after(zx::msec(1)));  // d. Sleep 1ms

  if ((status = SetSramBank(section_info.sram_bank)) != ZX_OK) {  // e. Select bank
    return status;
  }

  if ((status = WritePayload(section_info.address, section)) != ZX_OK) {  // f. Write section
    return status;
  }

  if ((status = WriteCopyCommand(section_info.copy_command)) != ZX_OK) {  // g. Write copy command
    return status;
  }

  if ((status = WaitUntilNotBusy()) != ZX_OK) {  // h. Wait until not busy
    return status;
  }

  if ((status = VerifyPayload(section_info.address, section)) != ZX_OK) {  // i. Verify section
    return status;
  }

  return ZX_OK;
}

zx_status_t Gt92xxDevice::WriteGwake(cpp20::span<const uint8_t> section) {
  zx_status_t status = WriteCopyCommand(0);  // 1. Clear copy command
  if (status != ZX_OK) {
    return status;
  }

  // 2. Send the four sections
  for (size_t i = 0; !section.empty(); i++, section = section.subspan(kFirmwareSectionSize)) {
    status = WriteGwakeOrLinkSection(kGwakeSections[i], section.subspan(0, kFirmwareSectionSize));
    if (status != ZX_OK) {
      return status;
    }
  }

  return ZX_OK;
}

zx_status_t Gt92xxDevice::WriteSs51Section(uint8_t section_number,
                                           cpp20::span<const uint8_t> section) {
  const auto section_info = kSs51Sections[section_number];

  zx_status_t status = HoldSs51AndDsp();  // a. Hold SS51 and DSP
  if (status != ZX_OK) {
    return status;
  }

  if ((status = SetScramble()) != ZX_OK) {  // b. Set scramble
    return status;
  }

  if ((status = SetSramBank(section_info.sram_bank)) != ZX_OK) {  // c. Select bank
    return status;
  }

  if ((status = EnableCodeAccess()) != ZX_OK) {  // d. Enable code access
    return status;
  }

  if ((status = WritePayload(section_info.address, section)) != ZX_OK) {  // e. Write section
    return status;
  }

  if ((status = HoldSs51ReleaseDsp()) != ZX_OK) {  // f. Release DSP
    return status;
  }

  zx::nanosleep(zx::deadline_after(zx::msec(1)));  // g. Sleep 1ms

  if ((status = WriteCopyCommand(section_info.copy_command)) != ZX_OK) {  // h. Write copy command
    return status;
  }

  if ((status = WaitUntilNotBusy()) != ZX_OK) {  // i. Wait until not busy
    return ZX_OK;
  }

  if ((status = SetSramBank(section_info.sram_bank)) != ZX_OK) {  // j.i. Select bank
    return status;
  }

  if ((status = EnableCodeAccess()) != ZX_OK) {  // j.ii. Enable code access
    return status;
  }

  if ((status = VerifyPayload(section_info.address, section)) != ZX_OK) {  // j.iii. Verify section
    return status;
  }

  if ((status = DisableCodeAccess()) != ZX_OK) {  // j.iv. Disable code access
    return status;
  }

  return ZX_OK;
}

zx_status_t Gt92xxDevice::WriteSs51(cpp20::span<const uint8_t> section) {
  // 1. Clear copy command
  zx_status_t status = WriteCopyCommand(0);
  if (status != ZX_OK) {
    return status;
  }

  // Sending only the first section
  if (section.size() == kFirmwareSectionSize) {
    return WriteSs51Section(0, section);
  }

  // 2. Write four SS51 sections, the first of which is all 0xff
  uint8_t ss51_buffer[kFirmwareSectionSize];
  memset(ss51_buffer, 0xff, sizeof(ss51_buffer));
  if ((status = WriteSs51Section(0, {ss51_buffer, sizeof(ss51_buffer)})) != ZX_OK) {
    return status;
  }

  // Skip the first section
  section = section.subspan(kFirmwareSectionSize);
  for (size_t i = 1; !section.empty(); i++, section = section.subspan(kFirmwareSectionSize)) {
    if ((status = WriteSs51Section(i, section.subspan(0, kFirmwareSectionSize))) != ZX_OK) {
      return status;
    }
  }

  return ZX_OK;
}

zx_status_t Gt92xxDevice::WriteDsp(cpp20::span<const uint8_t> section) {
  zx_status_t status = SetSramBank(kDspSection.sram_bank);  // 1. Select bank
  if (status != ZX_OK) {
    return status;
  }

  if ((status = HoldSs51AndDsp()) != ZX_OK) {  // 2. Hold SS51 and DSP
    return status;
  }

  if ((status = SetScramble()) != ZX_OK) {  // 3. Set scramble
    return status;
  }

  if ((status = ReleaseSs51AndDsp()) != ZX_OK) {  // 4. Release SS51 and DSP
    return status;
  }

  if ((status = WritePayload(kDspSection.address, section)) != ZX_OK) {  // 5. Write section
    return status;
  }

  if ((status = WriteCopyCommand(kDspSection.copy_command)) != ZX_OK) {  // 6. Write copy command
    return status;
  }

  if ((status = WaitUntilNotBusy()) != ZX_OK) {  // 7. Wait until not busy
    return ZX_OK;
  }

  if ((status = VerifyPayload(kDspSection.address, section)) != ZX_OK) {  // 8. Verify section
    return status;
  }

  return ZX_OK;
}

zx_status_t Gt92xxDevice::WriteBootOrBootIsp(SectionInfo section_info,
                                             cpp20::span<const uint8_t> section) {
  zx_status_t status = HoldSs51AndDsp();  // 1. Hold SS51 and DSP
  if (status != ZX_OK) {
    return status;
  }

  if ((status = SetScramble()) != ZX_OK) {  // 2. Set scramble
    return status;
  }

  if ((status = HoldSs51ReleaseDsp()) != ZX_OK) {  // 3. Release DSP
    return status;
  }

  if (section_info.copy_command == kBootIspSection.copy_command) {
    zx::nanosleep(zx::deadline_after(zx::msec(1)));  // 4. Sleep 1ms (Boot ISP only)
  }

  if ((status = SetSramBank(section_info.sram_bank)) != ZX_OK) {  // 5. Select bank
    return status;
  }

  if ((status = WritePayload(section_info.address, section)) != ZX_OK) {  // 6. Write section
    return status;
  }

  if ((status = WriteCopyCommand(section_info.copy_command)) != ZX_OK) {  // 7. Write copy command
    return status;
  }

  if ((status = WaitUntilNotBusy()) != ZX_OK) {  // 8. Wait until not busy
    return ZX_OK;
  }

  if ((status = VerifyPayload(section_info.address, section)) != ZX_OK) {  // 9. Verify section
    return status;
  }

  return ZX_OK;
}

zx_status_t Gt92xxDevice::WriteBoot(cpp20::span<const uint8_t> section) {
  return WriteBootOrBootIsp(kBootSection, section);
}

zx_status_t Gt92xxDevice::WriteBootIsp(cpp20::span<const uint8_t> section) {
  return WriteBootOrBootIsp(kBootIspSection, section);
}

zx_status_t Gt92xxDevice::WriteLink(cpp20::span<const uint8_t> section) {
  zx_status_t status =
      WriteGwakeOrLinkSection(kLinkSections[0], section.subspan(0, kLinkSection1Size));
  if (status != ZX_OK) {
    return status;
  }

  return WriteGwakeOrLinkSection(kLinkSections[1], section.subspan(kLinkSection1Size));
}

zx_status_t Gt92xxDevice::UpdateFirmwareIfNeeded() {
  // 1. Verify firmware
  zx::result<fzl::VmoMapper> firmware_mapper = LoadAndVerifyFirmware();
  if (firmware_mapper.is_error() && firmware_mapper.error_value() == ZX_ERR_NOT_FOUND) {
    // Just continue if the driver package doesn't include firmware.
    return ZX_OK;
  }
  if (firmware_mapper.is_error()) {
    return firmware_mapper.error_value();
  }

  // 2 - 3. Verify firmware is appropriate for the hardware
  if (!IsFirmwareApplicable(firmware_mapper.value())) {
    return ZX_OK;
  }

  zxlogf(INFO, "Starting firmware update");

  // 5. Enter update mode
  zx_status_t status = EnterUpdateMode();
  if (status != ZX_OK) {
    return status;
  }

  cpp20::span<const uint8_t> firmware(
      reinterpret_cast<const uint8_t*>(firmware_mapper.value().start()) + kFirmwareHeaderSize,
      firmware_mapper.value().size() - kFirmwareHeaderSize);

  // 6. Write DSP ISP
  const cpp20::span dsp_isp = firmware.subspan(firmware.size() - kDspIspSize);
  if ((status = WriteDspIsp(dsp_isp)) != ZX_OK) {
    return status;
  }
  firmware = firmware.subspan(0, firmware.size() - kDspIspSize);

  // 7. Write Gwake
  status = WriteGwake(firmware.subspan(firmware.size() - kFirmwareTotalSectionSize));
  if (status != ZX_OK) {
    return status;
  }
  firmware = firmware.subspan(0, firmware.size() - kFirmwareTotalSectionSize);

  // 8. Write SS51
  const cpp20::span ss51_first_section = firmware.subspan(0, kFirmwareSectionSize);
  if ((status = WriteSs51(firmware.subspan(0, kFirmwareTotalSectionSize))) != ZX_OK) {
    return ZX_OK;
  }
  firmware = firmware.subspan(kFirmwareTotalSectionSize);

  // 9. Write DSP
  if ((status = WriteDsp(firmware.subspan(0, kDspSize))) != ZX_OK) {
    return status;
  }
  firmware = firmware.subspan(kDspSize);

  // 10. Write boot
  if ((status = WriteBoot(firmware.subspan(0, kBootSize))) != ZX_OK) {
    return status;
  }
  firmware = firmware.subspan(kBootSize);

  // 11. Write boot ISP
  if ((status = WriteBootIsp(firmware.subspan(0, kBootIspSize))) != ZX_OK) {
    return status;
  }
  firmware = firmware.subspan(kBootIspSize);

  // 12. Write link
  if ((status = WriteLink(firmware.subspan(0, kLinkSection1Size + kLinkSection2Size))) != ZX_OK) {
    return status;
  }
  firmware = firmware.subspan(kLinkSection1Size + kLinkSection2Size);

  // 13.1 - 13.11. Send the real first SS51 section
  if ((status = WriteSs51(ss51_first_section)) != ZX_OK) {
    return status;
  }

  // 13.12. Enable DSP code download
  if ((status = WriteCopyCommand(kEnableDspCodeDownloadCommand)) != ZX_OK) {
    return status;
  }

  // 13.13. Release SS51 and hold DSP
  if ((status = ReleaseSs51HoldDsp()) != ZX_OK) {
    return status;
  }

  // 14. Leave update mode
  LeaveUpdateMode();

  zxlogf(INFO, "Firmware update finished");
  firmware_status_ = kFirmwareUpdated;
  return ZX_OK;
}

}  // namespace goodix
