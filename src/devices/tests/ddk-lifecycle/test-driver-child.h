// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fit/function.h>

#include <optional>

#include <ddktl/device.h>
#include <fbl/ref_counted.h>
#include <fbl/ref_ptr.h>

class TestLifecycleDriverChild;
using DeviceType =
    ddk::Device<TestLifecycleDriverChild, ddk::Initializable, ddk::Unbindable, ddk::Openable>;

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
  void DdkUnbind(ddk::UnbindTxn txn);
  void DdkRelease();
  void AsyncRemove(fit::function<void()> callback);
  void CompleteUnbind();
  zx_status_t DdkOpen(zx_device_t** dev_out, uint32_t flags);

  zx_status_t CompleteInit();

 private:
  // Whether we should immediately reply to the init hook.
  bool complete_init_ = false;
  bool replied_to_init_ = false;
  bool async_remove_ = false;
  // The status passed to device_init_reply.
  zx_status_t init_status_ = ZX_OK;
  std::optional<fit::function<void()>> unbind_callback_;
  std::optional<ddk::InitTxn> init_txn_;
  std::optional<ddk::UnbindTxn> unbind_txn_;
};

class InstanceDevice;
using InstanceDeviceType = ddk::Device<InstanceDevice>;
class InstanceDevice : public InstanceDeviceType {
 public:
  InstanceDevice(zx_device_t* parent) : InstanceDeviceType(parent) {}
  zx_status_t Add(zx_device_t** dev_out) {
    zx_status_t result = DdkAdd("test-child", DEVICE_ADD_INSTANCE);
    if (result != ZX_OK) {
      return result;
    }
    *dev_out = zxdev();
    return ZX_OK;
  }
  void DdkRelease() { delete this; }
};
