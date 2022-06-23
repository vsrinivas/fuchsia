// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_TESTS_NONBINDABLE_NONBINDABLE_H_
#define SRC_DEVICES_TESTS_NONBINDABLE_NONBINDABLE_H_

#include <ddktl/device.h>

namespace auto_bind {

class Nonbindable;
using DeviceType = ddk::Device<Nonbindable, ddk::Initializable>;
class Nonbindable : public DeviceType {
 public:
  explicit Nonbindable(zx_device_t* parent) : DeviceType(parent) {}
  virtual ~Nonbindable() = default;

  static zx_status_t Bind(void* ctx, zx_device_t* dev);
  zx_status_t Bind();
  void DdkInit(ddk::InitTxn txn);
  void DdkRelease();
};

}  // namespace auto_bind

#endif  // SRC_DEVICES_TESTS_NONBINDABLE_NONBINDABLE_H_
