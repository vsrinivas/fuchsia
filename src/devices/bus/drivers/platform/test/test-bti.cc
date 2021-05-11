// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/hardware/btitest/llcpp/fidl.h>
#include <lib/ddk/debug.h>
#include <lib/ddk/driver.h>
#include <lib/ddk/platform-defs.h>
#include <lib/device-protocol/pdev.h>

#include <memory>

#include <ddktl/device.h>
#include <ddktl/fidl.h>

#include "src/devices/bus/drivers/platform/test/test-bti-bind.h"

namespace {

class TestBti;
using DeviceType =
    ddk::Device<TestBti, ddk::Messageable<fuchsia_hardware_btitest::BtiDevice>::Mixin>;

class TestBti : public DeviceType, public fidl::WireServer<fuchsia_hardware_btitest::BtiDevice> {
 public:
  explicit TestBti(zx_device_t* parent) : DeviceType(parent) {}

  static zx_status_t Create(void*, zx_device_t* parent);

  void DdkRelease() { delete this; }

  void GetKoid(GetKoidRequestView request, GetKoidCompleter::Sync& completer) override;
  void Crash(CrashRequestView request, CrashCompleter::Sync&) override { __builtin_abort(); }
};

zx_status_t TestBti::Create(void*, zx_device_t* parent) {
  auto device = std::make_unique<TestBti>(parent);

  zx_status_t status = device->DdkAdd("test-bti");
  if (status != ZX_OK) {
    zxlogf(ERROR, "DdkAdd failed: %d", status);
    return status;
  }
  __UNUSED auto dummy = device.release();

  return ZX_OK;
}
void TestBti::GetKoid(GetKoidRequestView request, GetKoidCompleter::Sync& completer) {
  ddk::PDev pdev(parent());
  if (!pdev.is_valid()) {
    completer.Close(ZX_ERR_INTERNAL);
    return;
  }
  zx::bti bti;
  zx_status_t status = pdev.GetBti(0, &bti);
  if (status != ZX_OK) {
    completer.Close(status);
    return;
  }

  zx_info_handle_basic_t info;
  status = bti.get_info(ZX_INFO_HANDLE_BASIC, &info, sizeof(info), nullptr, nullptr);
  if (status != ZX_OK) {
    completer.Close(status);
    return;
  }

  completer.Reply(info.koid);
}

static zx_driver_ops_t test_driver_ops = {
    .version = DRIVER_OPS_VERSION,
    .bind = TestBti::Create,
};

}  // namespace

ZIRCON_DRIVER(test_bti, test_driver_ops, "zircon", "0.1");
