// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_TESTS_AUTOBIND_AUTOBIND_H_
#define SRC_DEVICES_TESTS_AUTOBIND_AUTOBIND_H_

#include <lib/inspect/cpp/inspect.h>

#include <ddktl/device.h>

namespace auto_bind {

class AutoBind;
using DeviceType = ddk::Device<AutoBind, ddk::Initializable>;
class AutoBind : public DeviceType {
 public:
  explicit AutoBind(zx_device_t* parent) : DeviceType(parent) {}
  virtual ~AutoBind() = default;

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

}  // namespace auto_bind

#endif  // SRC_DEVICES_TESTS_AUTOBIND_AUTOBIND_H_
