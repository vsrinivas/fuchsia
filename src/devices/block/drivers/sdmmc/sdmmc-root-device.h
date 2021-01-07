// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_BLOCK_DRIVERS_SDMMC_SDMMC_ROOT_DEVICE_H_
#define SRC_DEVICES_BLOCK_DRIVERS_SDMMC_SDMMC_ROOT_DEVICE_H_

#include <fuchsia/hardware/sdmmc/cpp/banjo.h>
#include <threads.h>

#include <atomic>

#include <ddktl/device.h>

#include "sdio-controller-device.h"
#include "sdmmc-block-device.h"

namespace sdmmc {

class SdmmcRootDevice;
using SdmmcRootDeviceType = ddk::Device<SdmmcRootDevice>;

class SdmmcRootDevice : public SdmmcRootDeviceType {
 public:
  static zx_status_t Bind(void* ctx, zx_device_t* parent);

  void DdkRelease();

  zx_status_t Init();

 private:
  SdmmcRootDevice(zx_device_t* parent, const ddk::SdmmcProtocolClient& host)
      : SdmmcRootDeviceType(parent), host_(host) {}

  int WorkerThread();

  const ddk::SdmmcProtocolClient host_;

  thrd_t worker_thread_ = 0;
};

}  // namespace sdmmc

#endif  // SRC_DEVICES_BLOCK_DRIVERS_SDMMC_SDMMC_ROOT_DEVICE_H_
