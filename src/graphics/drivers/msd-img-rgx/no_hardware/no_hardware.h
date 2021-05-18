// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_DRIVERS_MSD_IMG_RGX_NO_HARDWARE_NO_HARDWARE_H_
#define SRC_GRAPHICS_DRIVERS_MSD_IMG_RGX_NO_HARDWARE_NO_HARDWARE_H_

#include <fuchsia/gpu/magma/llcpp/fidl.h>
#include <lib/ddk/debug.h>
#include <lib/ddk/device.h>
#include <lib/ddk/driver.h>
#include <lib/ddk/platform-defs.h>

#include <memory>
#include <mutex>

#include <ddktl/device.h>

#include "img-sys-device.h"
#include "magma_util/macros.h"
#include "sys_driver/magma_driver.h"

class NoHardwareGpu;

using DeviceType = ddk::Device<NoHardwareGpu, ddk::Messageable<fuchsia_gpu_magma::Device>::Mixin>;

class NoHardwareGpu : public DeviceType, public ImgSysDevice {
 public:
  NoHardwareGpu(zx_device_t* parent) : DeviceType(parent) {}

  virtual ~NoHardwareGpu();

  zx_status_t Bind();
  void DdkRelease();

  void Query2(Query2RequestView request, Query2Completer::Sync& _completer) override;
  void QueryReturnsBuffer(QueryReturnsBufferRequestView request,
                          QueryReturnsBufferCompleter::Sync& _completer) override;
  void Connect(ConnectRequestView request, ConnectCompleter::Sync& _completer) override;
  void DumpState(DumpStateRequestView request, DumpStateCompleter::Sync& _completer) override;
  void TestRestart(TestRestartRequestView request, TestRestartCompleter::Sync& _completer) override;
  void GetUnitTestStatus(GetUnitTestStatusRequestView request,
                         GetUnitTestStatusCompleter::Sync& _completer) override;
  void GetIcdList(GetIcdListRequestView request, GetIcdListCompleter::Sync& completer) override {
    completer.Close(ZX_ERR_NOT_SUPPORTED);
  }

  zx_status_t PowerUp() override;
  zx_status_t PowerDown() override;
  void* device() override { return parent(); }

 private:
  bool StartMagma() MAGMA_REQUIRES(magma_mutex_);
  void StopMagma() MAGMA_REQUIRES(magma_mutex_);

  std::mutex magma_mutex_;
  std::unique_ptr<MagmaDriver> magma_driver_ MAGMA_GUARDED(magma_mutex_);
  std::shared_ptr<MagmaSystemDevice> magma_system_device_ MAGMA_GUARDED(magma_mutex_);
};

#endif  // SRC_GRAPHICS_DRIVERS_MSD_IMG_RGX_NO_HARDWARE_NO_HARDWARE_H_
