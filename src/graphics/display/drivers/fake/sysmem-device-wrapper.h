// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_DISPLAY_DRIVERS_FAKE_SYSMEM_DEVICE_WRAPPER_H_
#define SRC_GRAPHICS_DISPLAY_DRIVERS_FAKE_SYSMEM_DEVICE_WRAPPER_H_

#include <fuchsia/hardware/sysmem/c/banjo.h>
#include <fuchsia/hardware/sysmem/cpp/banjo.h>

#include <ddktl/device.h>

#include "src/devices/sysmem/drivers/sysmem/driver.h"

namespace display {

// Clients of FakeDisplayDeviceTree pass a SysmemDeviceWrapper into the constructor to provide a
// sysmem implementation to the display driver, with the goal of supporting the following use cases:
//   - display driver unit tests want to use a self-contained/hermetic sysmem implementation, to
//     improve reliability of test results.
//   - system integration tests may want to use the "global" sysmem so that multiple components
//     can use it to coordinate memory allocation, for example tests which involve Scenic, Magma,
//     and the display driver.
class SysmemDeviceWrapper {
 public:
  virtual ~SysmemDeviceWrapper() = default;

  virtual const sysmem_protocol_t* proto() const = 0;
  virtual const zx_device_t* device() const = 0;
  virtual zx_status_t Bind() = 0;
};

// Convenient implementation of SysmemDeviceWrapper which can be used to wrap both
// sysmem_device::Driver and display::SysmemProxyDevice (the initial two usages of
// SysmemDeviceWrapper).
template <typename T>
class GenericSysmemDeviceWrapper : public SysmemDeviceWrapper {
 public:
  GenericSysmemDeviceWrapper()
      : sysmem_ctx_(std::make_unique<sysmem_driver::Driver>()),
        owned_sysmem_(std::make_unique<T>(fake_ddk::kFakeParent, sysmem_ctx_.get())) {
    sysmem_ = owned_sysmem_.get();
  }

  const sysmem_protocol_t* proto() const override { return sysmem_->proto(); }
  const zx_device_t* device() const override { return sysmem_->device(); }
  zx_status_t Bind() override {
    zx_status_t status = sysmem_->Bind();
    if (status == ZX_OK) {
      // DDK takes ownership of sysmem and DdkRelease will release it.
      owned_sysmem_.release();
    }
    return status;
  }

 private:
  std::unique_ptr<sysmem_driver::Driver> sysmem_ctx_;
  std::unique_ptr<T> owned_sysmem_;
  T* sysmem_{};
};

}  // namespace display

#endif  // SRC_GRAPHICS_DISPLAY_DRIVERS_FAKE_SYSMEM_DEVICE_WRAPPER_H_
