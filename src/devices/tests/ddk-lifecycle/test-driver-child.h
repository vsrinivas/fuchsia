// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <optional>

#include <ddktl/device.h>
#include <fbl/ref_counted.h>
#include <fbl/ref_ptr.h>

class TestLifecycleDriverChild;
using DeviceType = ddk::Device<TestLifecycleDriverChild, ddk::Initializable, ddk::UnbindableNew>;

class TestLifecycleDriverChild : public DeviceType,
                                 public fbl::RefCounted<TestLifecycleDriverChild> {
 public:
  static zx_status_t Create(zx_device_t* parent, bool complete_init, zx_status_t init_status,
                            fbl::RefPtr<TestLifecycleDriverChild>* out_device);

  explicit TestLifecycleDriverChild(zx_device_t* parent, bool complete_init,
                                    zx_status_t init_status)
      : DeviceType(parent), complete_init_(complete_init), init_status_(init_status) {}
  ~TestLifecycleDriverChild() {}

  void DdkInit(ddk::InitTxn txn);
  void DdkUnbindNew(ddk::UnbindTxn txn);
  void DdkRelease();

  zx_status_t CompleteInit();

 private:
  // Whether we should immediately reply to the init hook.
  bool complete_init_ = false;
  bool replied_to_init_ = false;
  // The status passed to device_init_reply.
  zx_status_t init_status_ = ZX_OK;
  std::optional<ddk::InitTxn> init_txn_;
};
