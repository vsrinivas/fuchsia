// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fake-i2c/fake-i2c.h>
#include <lib/fake_ddk/fake_ddk.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <fbl/unique_fd.h>
#include <zxtest/zxtest.h>

#include "ft8201.h"

zx_status_t load_firmware(zx_device_t* device, const char* path, zx_handle_t* fw, size_t* size) {
  const char kPkgFirmwarePath[] = "/pkg/data/firmware/" FT8201_FIRMWARE_PATH;
  const char kPkgPrambootPath[] = "/pkg/data/firmware/" FT8201_PRAMBOOT_PATH;

  const char* full_path = nullptr;
  if (strcmp(path, FT8201_FIRMWARE_PATH) == 0) {
    full_path = kPkgFirmwarePath;
  } else if (strcmp(path, FT8201_PRAMBOOT_PATH) == 0) {
    full_path = kPkgPrambootPath;
  } else {
    return ZX_ERR_NOT_FOUND;
  }

  fbl::unique_fd firmware_fd(open(full_path, O_RDONLY));
  if (!firmware_fd) {
    return ZX_ERR_NOT_FOUND;
  }

  struct stat statbuf = {};
  int ret = fstat(firmware_fd.get(), &statbuf);
  if (ret != 0) {
    return ZX_ERR_NOT_FOUND;
  }

  zx::vmo firmware_vmo;
  zx_status_t status = zx::vmo::create(statbuf.st_size, 0, &firmware_vmo);
  if (status != ZX_OK) {
    printf("Failed to create VMO: %d\n", status);
    return status;
  }

  void* const firmware =
      mmap(nullptr, statbuf.st_size, PROT_READ, MAP_PRIVATE, firmware_fd.get(), 0);
  if (firmware == MAP_FAILED) {
    return ZX_ERR_IO;
  }

  if ((status = firmware_vmo.write(firmware, 0, statbuf.st_size)) == ZX_OK) {
    *fw = firmware_vmo.release();
    *size = statbuf.st_size;
  }

  munmap(firmware, statbuf.st_size);
  return status;
}

namespace touch {

class FakeTouchFirmwareDevice : public fake_i2c::FakeI2c {
 protected:
  zx_status_t Transact(const uint8_t* write_buffer, size_t write_buffer_size, uint8_t* read_buffer,
                       size_t* read_buffer_size) override {
    if (write_buffer_size < 1) {
      return ZX_ERR_TIMED_OUT;
    }

    const uint8_t address = write_buffer[0];
    write_buffer++;

    *read_buffer_size = 0;

    if (address == 0xa3) {
      read_buffer[0] = 0x82;
      *read_buffer_size = 1;
    } else if (address == 0xa6) {
      read_buffer[0] = 0x4;  // Report a firmware version different than the binary we have.
      *read_buffer_size = 1;
    } else if (address == 0x90) {
      read_buffer[0] = boot_id_ >> 8;
      read_buffer[1] = boot_id_ & 0xff;
      *read_buffer_size = 2;
    } else if (address == 0xae) {  // Write pramboot data command.
      const uint16_t length = (write_buffer[3] << 8) | write_buffer[4];
      if (length > 128 || write_buffer_size != 1 + 3 + 2 + length) {
        return ZX_ERR_TIMED_OUT;
      }
      pramboot_ecc_ = CalculateEcc(&write_buffer[5], length, pramboot_ecc_);
    } else if (address == 0xcc) {  // Read pramboot ECC command.
      read_buffer[0] = pramboot_ecc_;
      *read_buffer_size = 1;
    } else if (address == 0x08) {
      boot_id_ = 0x80c6;           // Report the pramboot ID next time.
    } else if (address == 0x61) {  // Erase status command.
      flash_status_ = 0xf0aa;
    } else if (address == 0x6a) {  // Flash status command.
      read_buffer[0] = flash_status_ >> 8;
      read_buffer[1] = flash_status_ & 0xff;
      *read_buffer_size = 2;
    } else if (address == 0xbf) {  // Write firmware data command.
      const uint32_t address = (write_buffer[0] << 16) | (write_buffer[1] << 8) | (write_buffer[2]);
      const uint16_t length = (write_buffer[3] << 8) | write_buffer[4];

      if (length > 128 || write_buffer_size != 1 + 3 + 2 + length) {
        return ZX_ERR_TIMED_OUT;
      }

      firmware_ecc_ = CalculateEcc(&write_buffer[5], length, firmware_ecc_);
      flash_status_ = 0x1000 + (address / length);
    } else if (address == 0x65) {  // Firmware ECC calculation command.
      flash_status_ = 0xf055;
    } else if (address == 0x66) {  // Read firmware ECC command.
      read_buffer[0] = firmware_ecc_;
      *read_buffer_size = 1;
    }

    return ZX_OK;
  }

 private:
  uint16_t boot_id_ = 0x8006;
  uint8_t pramboot_ecc_ = 0;
  uint8_t firmware_ecc_ = 0;
  uint16_t flash_status_ = 0;

  static uint8_t CalculateEcc(const uint8_t* const buffer, const size_t size, uint8_t initial) {
    for (size_t i = 0; i < size; i++) {
      initial ^= buffer[i];
    }
    return initial;
  }
};

TEST(Ft8201FirmwareTest, FirmwareDownload) {
  FakeTouchFirmwareDevice i2c_dev;
  Ft8201Device dut(fake_ddk::kFakeParent, i2c_dev.GetProto());
  EXPECT_OK(dut.FirmwareDownloadIfNeeded());
}

}  // namespace touch
