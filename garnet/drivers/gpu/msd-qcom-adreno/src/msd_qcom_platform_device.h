// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MSD_QCOM_PLATFORM_DEVICE_H
#define MSD_QCOM_PLATFORM_DEVICE_H

#include <platform_device.h>

class MsdQcomPlatformDevice {
 public:
  virtual ~MsdQcomPlatformDevice() = default;

  MsdQcomPlatformDevice(std::unique_ptr<magma::PlatformDevice> platform_device)
      : platform_device_(std::move(platform_device)) {}

  magma::PlatformDevice* platform_device() { return platform_device_.get(); }

  virtual uint32_t GetChipId() const = 0;

  // Returns the size of the on-chip graphics memory
  virtual uint32_t GetGmemSize() const = 0;

  virtual void ResetGmu() = 0;

  static std::unique_ptr<MsdQcomPlatformDevice> Create(void* platform_device_handle);

 protected:
  std::unique_ptr<magma::PlatformDevice> platform_device_;
};

#endif  // MSD_QCOM_PLATFORM_DEVICE_H
