// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ethertap.h"

#include <lib/fake_ddk/fake_ddk.h>
#include <lib/zx/process.h>

#include <string>
#include <thread>
#include <vector>

#include <ddk/debug.h>
#include <ddk/device.h>
#include <zxtest/zxtest.h>

class FakeEthertapMiscParent : public ddk::Device<FakeEthertapMiscParent>, public fake_ddk::Bind {
 public:
  FakeEthertapMiscParent() : ddk::Device<FakeEthertapMiscParent>(fake_ddk::kFakeParent) {}

  ~FakeEthertapMiscParent() {
    WaitForChildRemoval();
    if (tap_ctl_) {
      tap_ctl_->DdkRelease();
    }
    if (child_device_) {
      child_device_->device->DdkRelease();
    }
  }

  // Returns immediately if this was called previously.
  zx_status_t WaitForChildRemoval() {
    if (child_device_) {
      return sync_completion_wait(&child_device_->completion, ZX_TIME_INFINITE);
    }
    return ZX_OK;
  }

  zx_status_t DeviceAdd(zx_driver_t* drv, zx_device_t* parent, device_add_args_t* args,
                        zx_device_t** out) override {
    if (parent == fake_ddk::kFakeParent) {
      tap_ctl_ = static_cast<eth::TapCtl*>(args->ctx);
    } else if (parent == tap_ctl_->zxdev()) {
      if (child_device_) {
        ADD_FAILURE("Unexpected additional child device added");
        return ZX_ERR_INTERNAL;
      }
      child_device_ = ChildDevice();
      child_device_->name = args->name;
      child_device_->device = static_cast<eth::TapDevice*>(args->ctx);
      // Set the device's unbind hook that fake_ddk::Bind::DeviceAsyncRemove will call.
      unbind_op_ = args->ops->unbind;
      op_ctx_ = args->ctx;
    } else {
      ADD_FAILURE("Unrecognized parent");
    }
    *out = reinterpret_cast<zx_device_t*>(args->ctx);
    return ZX_OK;
  }

  zx_status_t DeviceRemove(zx_device_t* device) override {
    if (child_device_ && reinterpret_cast<zx_device_t*>(child_device_->device) == device) {
      sync_completion_signal(&child_device_->completion);
    }
    return ZX_OK;
  }

  eth::TapCtl* tap_ctl() { return tap_ctl_; }

  eth::TapDevice* tap_device() { return child_device_->device; }

  void DdkRelease() {}

  const char* DeviceGetName(zx_device_t* device) override {
    if (device == tap_ctl_->zxdev()) {
      return "tapctl";
    }
    if (child_device_ && child_device_->device->zxdev() == device) {
      return child_device_->name.c_str();
    }
    return "";
  }

 private:
  struct ChildDevice {
    std::string name;
    eth::TapDevice* device;
    sync_completion_t completion;
  };

  eth::TapCtl* tap_ctl_;
  std::optional<ChildDevice> child_device_;
};

class EthertapTests : public zxtest::Test {
 public:
  EthertapTests() {
    fbl::Array<fake_ddk::ProtocolEntry> protocols(new fake_ddk::ProtocolEntry[1], 1);
    protocols[0] = {ZX_PROTOCOL_MISC_PARENT, {nullptr, nullptr}};
    ddk_.SetProtocols(std::move(protocols));
  }

  void SetupTapCtlMessenger() {
    messenger_.SetMessageOp(ddk_.tap_ctl(),
                            [](void* ctx, fidl_incoming_msg_t* msg, fidl_txn_t* txn) {
                              return static_cast<eth::TapCtl*>(ctx)->DdkMessage(msg, txn);
                            });
  }

 protected:
  FakeEthertapMiscParent ddk_;
  fake_ddk::FidlMessenger messenger_;
};

TEST_F(EthertapTests, TestLongNameMatches) {
  ASSERT_OK(eth::TapCtl::Create(nullptr, fake_ddk::kFakeParent));
  SetupTapCtlMessenger();
  const char* long_name = "012345678901234567890123456789";
  auto len = strlen(long_name);
  ASSERT_EQ(len, fuchsia_hardware_ethertap_MAX_NAME_LENGTH);
  fuchsia_hardware_ethertap_Config config = {0, 0, 1500, {1, 2, 3, 4, 5, 6}};
  zx::channel tap, req;
  ASSERT_OK(zx::channel::create(0, &tap, &req));
  zx_status_t status;
  ASSERT_OK(fuchsia_hardware_ethertap_TapControlOpenDevice(messenger_.local().get(), long_name, len,
                                                           &config, tap.release(), &status));
  ASSERT_OK(status);
  ASSERT_STR_EQ(long_name, ddk_.tap_device()->name());
}

TEST_F(EthertapTests, TestShortNameMatches) {
  ASSERT_OK(eth::TapCtl::Create(nullptr, fake_ddk::kFakeParent));
  SetupTapCtlMessenger();
  const char* short_name = "abc";
  auto len = strlen(short_name);
  fuchsia_hardware_ethertap_Config config = {0, 0, 1500, {1, 2, 3, 4, 5, 6}};
  zx::channel tap, req;
  ASSERT_OK(zx::channel::create(0, &tap, &req));
  zx_status_t status;
  ASSERT_OK(fuchsia_hardware_ethertap_TapControlOpenDevice(messenger_.local().get(), short_name,
                                                           len, &config, tap.release(), &status));
  ASSERT_OK(status);
  ASSERT_STR_EQ(short_name, ddk_.tap_device()->name());
}

// This tests triggering the unbind hook via DdkAsyncRemove and verifying the unbind reply occurs.
TEST_F(EthertapTests, UnbindSignalsWorkerThread) {
  ASSERT_OK(eth::TapCtl::Create(nullptr, fake_ddk::kFakeParent));
  SetupTapCtlMessenger();
  fuchsia_hardware_ethertap_Config config = {0, 0, 1500, {1, 2, 3, 4, 5, 6}};
  zx::channel tap, req;
  ASSERT_OK(zx::channel::create(0, &tap, &req));
  zx_status_t status;
  ASSERT_OK(fuchsia_hardware_ethertap_TapControlOpenDevice(messenger_.local().get(), "", 0, &config,
                                                           tap.release(), &status));
  ASSERT_OK(status);

  // This should run the device unbind hook, which signals the worker thread to reply to the
  // unbind txn and exit.
  ddk_.tap_device()->DdkAsyncRemove();
  // |DeviceRemove| should be called after the unbind txn is replied to.
  ASSERT_OK(ddk_.WaitForChildRemoval());
}
