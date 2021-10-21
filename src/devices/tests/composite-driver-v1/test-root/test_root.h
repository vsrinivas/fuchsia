// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_TESTS_COMPOSITE_DRIVER_V1_TEST_ROOT_TEST_ROOT_H_
#define SRC_DEVICES_TESTS_COMPOSITE_DRIVER_V1_TEST_ROOT_TEST_ROOT_H_

#include <lib/inspect/cpp/inspect.h>

#include <ddktl/device.h>

namespace test_root {

class TestRoot;
using DeviceType = ddk::Device<TestRoot, ddk::Initializable>;
class TestRoot : public DeviceType {
 public:
  explicit TestRoot(zx_device_t* parent) : DeviceType(parent) {}
  virtual ~TestRoot() = default;

  static zx_status_t Bind(void* ctx, zx_device_t* dev);
  zx_status_t Bind(const char* name, cpp20::span<const zx_device_prop_t> props);
  void DdkInit(ddk::InitTxn txn);
  void DdkRelease();

  // For inspect test.
  zx::vmo inspect_vmo() { return inspect_.DuplicateVmo(); }

 private:
  inspect::Inspector inspect_;
  inspect::BoolProperty is_bound = inspect_.GetRoot().CreateBool("is_bound", false);
};

}  // namespace test_root

#endif  // SRC_DEVICES_TESTS_COMPOSITE_DRIVER_V1_TEST_ROOT_TEST_ROOT_H_
