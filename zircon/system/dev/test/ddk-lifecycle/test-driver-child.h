// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddktl/device.h>
#include <fbl/ref_counted.h>
#include <fbl/ref_ptr.h>

class TestLifecycleDriverChild;
using DeviceType = ddk::Device<TestLifecycleDriverChild, ddk::UnbindableNew>;

class TestLifecycleDriverChild : public DeviceType,
                                 public fbl::RefCounted<TestLifecycleDriverChild> {
 public:
  static zx_status_t Create(zx_device_t* parent,
                            fbl::RefPtr<TestLifecycleDriverChild>* out_device);

  explicit TestLifecycleDriverChild(zx_device_t* parent) : DeviceType(parent) {}
  ~TestLifecycleDriverChild() {}

  void DdkUnbindNew(ddk::UnbindTxn txn) { txn.Reply(); }
  void DdkRelease();
};
