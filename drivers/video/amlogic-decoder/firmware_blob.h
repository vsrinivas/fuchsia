// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FIRMWARE_BLOB_H_
#define FIRMWARE_BLOB_H_

#include <ddk/device.h>
#include <ddk/driver.h>
#include <zx/vmo.h>

#include <map>
#include <string>

class FirmwareBlob {
 public:
  enum class FirmwareType {
    kMPEG12,
    kH264,
    kVp9Mmu,
    kVp9MmuG12a,
  };

  ~FirmwareBlob();

  zx_status_t LoadFirmware(zx_device_t* device);

  zx_status_t GetFirmwareData(FirmwareType firmware_type, uint8_t** data_out,
                              uint32_t* size_out);

 private:
  struct FirmwareCode {
    uint64_t offset;
    uint32_t size;
  };

  zx::vmo vmo_;
  uintptr_t ptr_ = 0;
  uint64_t fw_size_ = 0;
  std::map<std::string, FirmwareCode> firmware_code_;
};

#endif  // FIRMWARE_BLOB_H_
