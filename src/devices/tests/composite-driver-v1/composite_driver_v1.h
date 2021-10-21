// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_TESTS_COMPOSITE_DRIVER_V1_COMPOSITE_DRIVER_V1_H_
#define SRC_DEVICES_TESTS_COMPOSITE_DRIVER_V1_COMPOSITE_DRIVER_V1_H_

#include <lib/inspect/cpp/inspect.h>

#include <ddktl/device.h>

namespace composite_driver_v1 {

class CompositeDriverV1;
using DeviceType = ddk::Device<CompositeDriverV1, ddk::Initializable>;
class CompositeDriverV1 : public DeviceType {
 public:
  explicit CompositeDriverV1(zx_device_t* parent) : DeviceType(parent) {}
  virtual ~CompositeDriverV1() = default;

  static zx_status_t Bind(void* ctx, zx_device_t* dev);
  zx_status_t Bind();
  void DdkInit(ddk::InitTxn txn);
  void DdkRelease();

  // For inspect test.
  zx::vmo inspect_vmo() { return inspect_.DuplicateVmo(); }

 private:
  inspect::Inspector inspect_;
  inspect::BoolProperty is_bound = inspect_.GetRoot().CreateBool("is_bound", false);
};

}  // namespace composite_driver_v1

#endif  // SRC_DEVICES_TESTS_COMPOSITE_DRIVER_V1_COMPOSITE_DRIVER_V1_H_
