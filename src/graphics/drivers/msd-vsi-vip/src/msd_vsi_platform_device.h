// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MSD_VSI_PLATFORM_DEVICE_H
#define MSD_VSI_PLATFORM_DEVICE_H

#include <platform_device.h>

class MsdVsiPlatformDevice {
 public:
  virtual ~MsdVsiPlatformDevice() = default;

  MsdVsiPlatformDevice(std::unique_ptr<magma::PlatformDevice> platform_device)
      : platform_device_(std::move(platform_device)) {}

  magma::PlatformDevice* platform_device() { return platform_device_.get(); }

  virtual uint64_t GetExternalSramPhysicalBase() const = 0;

  static std::unique_ptr<MsdVsiPlatformDevice> Create(void* platform_device_handle);

 protected:
  std::unique_ptr<magma::PlatformDevice> platform_device_;
};

#endif  // MSD_VSI_PLATFORM_DEVICE_H
