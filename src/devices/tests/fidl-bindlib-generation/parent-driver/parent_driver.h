// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_TESTS_FIDL_BINDLIB_GENERATION_PARENT_DRIVER_PARENT_DRIVER_H_
#define SRC_DEVICES_TESTS_FIDL_BINDLIB_GENERATION_PARENT_DRIVER_PARENT_DRIVER_H_

#include <lib/inspect/cpp/inspect.h>

#include <ddktl/device.h>

namespace parent_driver {

class ParentDriver;
using DeviceType = ddk::Device<ParentDriver>;
class ParentDriver : public DeviceType {
 public:
  explicit ParentDriver(zx_device_t* dev) : DeviceType(dev) {}
  virtual ~ParentDriver() = default;

  static zx_status_t Bind(void* ctx, zx_device_t* dev);
  void DdkRelease();

  // For inspect test.
  zx::vmo inspect_vmo() const { return inspect_.DuplicateVmo(); }

 private:
  inspect::Inspector inspect_;
  inspect::BoolProperty is_bound = inspect_.GetRoot().CreateBool("is_bound", false);
};

}  // namespace parent_driver

#endif  // SRC_DEVICES_TESTS_FIDL_BINDLIB_GENERATION_PARENT_DRIVER_PARENT_DRIVER_H_
