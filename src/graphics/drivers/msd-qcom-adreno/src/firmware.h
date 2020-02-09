// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FIRMWARE_H
#define FIRMWARE_H

#include <memory>

#include <magma_util/macros.h>

#include "address_space.h"
#include "gpu_mapping.h"
#include "msd_qcom_platform_device.h"

class Firmware {
 public:
  static std::unique_ptr<Firmware> Create(MsdQcomPlatformDevice* device);

  uint64_t size() {
    DASSERT(size_ > 0);
    return size_;
  }

  uint64_t gpu_addr() {
    DASSERT(gpu_mapping_);
    return gpu_mapping_->gpu_addr();
  }

  bool Map(std::shared_ptr<AddressSpace> address_space);

  static const char* GetFirmwareFilename(uint32_t chip_id);

 private:
  bool Init(MsdQcomPlatformDevice* device);

  std::shared_ptr<magma::PlatformBuffer> buffer_;
  uint64_t size_ = 0;
  std::unique_ptr<GpuMapping> gpu_mapping_;

  friend class TestFirmware;
};

#endif  // FIRMWARE_H
