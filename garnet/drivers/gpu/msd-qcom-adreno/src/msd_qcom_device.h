// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MSD_QCOM_DEVICE_H
#define MSD_QCOM_DEVICE_H

#include <magma_util/register_io.h>

#include "msd_qcom_platform_device.h"

class MsdQcomDevice {
 public:
  static std::unique_ptr<MsdQcomDevice> Create(void* device_handle);

  uint32_t GetChipId() { return qcom_platform_device_->GetChipId(); }

 private:
  bool Init(void* device_handle);

  std::unique_ptr<MsdQcomPlatformDevice> qcom_platform_device_;
  std::unique_ptr<magma::RegisterIo> register_io_;
};

#endif  // MSD_QCOM_DEVICE_H
