// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <endian.h>
#include <lib/fzl/vmo-mapper.h>

#include <climits>

#include <ddk/debug.h>

#include "src/camera/drivers/sensors/imx227/imx227.h"
#include "src/camera/drivers/sensors/imx227/imx227_otp_config.h"

namespace camera {

fit::result<zx::vmo, zx_status_t> Imx227Device::OtpRead() {
  fzl::VmoMapper mapper;
  zx::vmo vmo;
  zx_status_t status =
      mapper.CreateAndMap(OTP_TOTAL_SIZE, ZX_VM_PERM_READ | ZX_VM_PERM_WRITE, nullptr, &vmo);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: failed to create and map VMO\n", __func__);
    return fit::error(status);
  }

  auto dest = static_cast<unsigned char*>(mapper.start());

  // Endian-flipped OTP start address to read through I2C channel
  const uint16_t kPageStartRegister = htobe16(OTP_PAGE_START);

  for (uint32_t page_index = 0; page_index < OTP_PAGE_NUM; ++page_index) {
    // TODO(nzo): does this check need to be in the loop?
    if (!ValidateSensorID()) {
      status = ZX_ERR_INTERNAL;
      zxlogf(ERROR, "%s: could not read Sensor ID\n", __func__);
      return fit::error(status);
    }

    // Select page to read from and enable OTP page reads
    Write8(OTP_PAGE_SELECT, page_index);
    Write8(OTP_READ_ENABLE, 1);

    // Optional check
    const auto kReadStatus = Read8(OTP_ACCESS_STATUS);
    if (kReadStatus != 1) {
      status = ZX_ERR_IO;
      zxlogf(ERROR, "%s: read access could not be verified, access is %x\n", __func__, kReadStatus);
      return fit::error(status);
    }

    status = i2c_.WriteReadSync(reinterpret_cast<const uint8_t*>(&kPageStartRegister),
                                sizeof(kPageStartRegister), dest, OTP_PAGE_SIZE);
    if (status != ZX_OK) {
      zxlogf(ERROR, "%s: failed to read from I2C channel\n", __func__);
      return fit::error(status);
    }

    dest += OTP_PAGE_SIZE;
  }

  mapper.Unmap();
  return fit::ok(std::move(vmo));
}

bool Imx227Device::OtpValidate(const zx::vmo* vmo) {
  std::array<uint8_t, OTP_TOTAL_SIZE> data;
  vmo->read(&data, 0, OTP_TOTAL_SIZE);
  const auto kId = data[0];
  const auto kDay = data[1];
  const auto kMonth = data[2] & 0x0F;
  // Year starts from 2018 and is incremented by the stored value
  const auto kYear = ((data[2] & 0xF0) >> 4) + 2018;
  const auto kFactory = data[3];

  uint32_t checksum = 0;
  uint16_t checksum_target =
      (data[OTP_CHECKSUM_HIGH_START] << CHAR_BIT) | (data[OTP_CHECKSUM_LOW_START]);

  for (auto i = 0; i < OTP_CHECKSUM_HIGH_START; ++i) {
    checksum += data[i];
  }

  // Only lower two bytes are counted
  const uint16_t kChecksumMask = 0xFFFF;
  checksum &= kChecksumMask;

  if (checksum != checksum_target) {
    zxlogf(ERROR, "%s: checksum validation failed. Expected 0x%x, calculated 0x%x\n", __func__,
           checksum_target, checksum);
    return false;
  }

  zxlogf(INFO, "%s: ID %d, built on %d-%d-%d (mm-dd-yyyy), in factory %x\n", __func__, kId, kMonth,
         kDay, kYear, kFactory);
  return true;
}

}  // namespace camera
