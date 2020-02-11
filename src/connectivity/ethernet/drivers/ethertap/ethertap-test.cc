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
    for (auto& child : child_devices_) {
      sync_completion_wait(&child.completion, ZX_TIME_INFINITE);
    }
    if (tap_ctl_) {
      tap_ctl_->DdkRelease();
    }
    for (auto& child : child_devices_) {
      child.device->DdkRelease();
    }
  }

  zx_status_t DeviceAdd(zx_driver_t* drv, zx_device_t* parent, device_add_args_t* args,
                        zx_device_t** out) override {
    if (parent == fake_ddk::kFakeParent) {
      tap_ctl_ = static_cast<eth::TapCtl*>(args->ctx);
    } else if (parent == tap_ctl_->zxdev()) {
      auto& dev = child_devices_.emplace_back();
      dev.name = args->name;
      dev.device = static_cast<eth::TapDevice*>(args->ctx);
    } else {
      ADD_FAILURE("Unrecognized parent");
    }
    *out = reinterpret_cast<zx_device_t*>(args->ctx);
    return ZX_OK;
  }

  zx_status_t DeviceRemove(zx_device_t* device) override {
    for (auto& x : child_devices_) {
      if (reinterpret_cast<zx_device_t*>(x.device) == device) {
        sync_completion_signal(&x.completion);
      }
    }
    return ZX_OK;
  }

  eth::TapCtl* tap_ctl() { return tap_ctl_; }

  eth::TapDevice* tap_device(size_t idx) { return child_devices_[idx].device; }

  void DdkRelease() {}

  const char* DeviceGetName(zx_device_t* device) override {
    if (device == tap_ctl_->zxdev()) {
      return "tapctl";
    }
    for (auto& x : child_devices_) {
      if (x.device->zxdev() == device) {
        return x.name.c_str();
      }
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
  std::vector<ChildDevice> child_devices_;
};

class EthertapTests : public zxtest::Test {
 public:
  EthertapTests() {
    fbl::Array<fake_ddk::ProtocolEntry> protocols(new fake_ddk::ProtocolEntry[1], 1);
    protocols[0] = {ZX_PROTOCOL_MISC_PARENT, {nullptr, nullptr}};
    ddk_.SetProtocols(std::move(protocols));
  }

  void SetupTapCtlMessenger() {
    messenger_.SetMessageOp(ddk_.tap_ctl(), [](void* ctx, fidl_msg_t* msg, fidl_txn_t* txn) {
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
  ASSERT_STR_EQ(long_name, ddk_.tap_device(0)->name());
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
  ASSERT_STR_EQ(short_name, ddk_.tap_device(0)->name());
}
