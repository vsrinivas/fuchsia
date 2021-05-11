// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/test/fake-ddk-tester.h"

#include <zircon/syscalls.h>

// This trampolines the load_firmware() DDK entry point to the FakeDdkTester implementation.
zx_status_t load_firmware(zx_device_t* device, const char* path, zx_handle_t* fw, size_t* size) {
  return static_cast<wlan::testing::FakeDdkTester*>(fake_ddk::Bind::Instance())
      ->LoadFirmware(device, path, fw, size);
}

namespace wlan::testing {

FakeDdkTester::FakeDdkTester() = default;

FakeDdkTester::~FakeDdkTester() = default;

void FakeDdkTester::SetFirmware(std::string firmware) { firmware_ = std::move(firmware); }

wlan::iwlwifi::Device* FakeDdkTester::dev() { return dev_; }

const wlan::iwlwifi::Device* FakeDdkTester::dev() const { return dev_; }

const std::vector<wlan::iwlwifi::MacDevice*>& FakeDdkTester::macdevs() const { return macdevs_; }

fake_ddk::Bind& FakeDdkTester::ddk() { return *this; }

const fake_ddk::Bind& FakeDdkTester::ddk() const { return *this; }

std::string FakeDdkTester::GetFirmware() const { return firmware_; }

// FakeDdkTester implementation of the load_firmware() DDK entry point.
zx_status_t FakeDdkTester::LoadFirmware(zx_device_t* device, const char* path, zx_handle_t* fw,
                                        size_t* size) {
  zx_status_t status = ZX_OK;
  zx_handle_t vmo = ZX_HANDLE_INVALID;
  if ((status = zx_vmo_create(firmware_.size(), 0, &vmo)) != ZX_OK) {
    return status;
  }
  if ((status = zx_vmo_write(vmo, firmware_.data(), 0, firmware_.size())) != ZX_OK) {
    return status;
  }

  *fw = vmo;
  *size = firmware_.size();
  return ZX_OK;
}

// FakeDdkTester implementation of the device_add() entry point.
zx_status_t FakeDdkTester::DeviceAdd(zx_driver_t* drv, zx_device_t* parent, device_add_args_t* args,
                                     zx_device_t** out) {
  zx_status_t ret = Bind::DeviceAdd(drv, parent, args, out);
  if (ret == ZX_OK) {
    // On successful DeviceAdd() we save off the devices so that we can access them in the test
    // to take subsequent actions or to release its resources at the end of the test.
    if (parent == fake_ddk::kFakeParent) {
      // The top node for iwlwifi will be the SimDevice
      dev_ = static_cast<wlan::iwlwifi::Device*>(args->ctx);
    } else {
      // Everything else (i.e. children) will be of type MacDevice, created via
      // the WlanphyImplCreateIface() call.
      macdevs_.push_back(static_cast<wlan::iwlwifi::MacDevice*>(args->ctx));
    }
  }
  return ret;
}

}  // namespace wlan::testing
