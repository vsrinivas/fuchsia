// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_TESTS_COMPOSITE_DRIVER_V1_TEST_ROOT_TEST_ROOT_H_
#define SRC_DEVICES_TESTS_COMPOSITE_DRIVER_V1_TEST_ROOT_TEST_ROOT_H_

#include <fidl/fuchsia.composite.test/cpp/wire.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/inspect/cpp/inspect.h>
#include <lib/sys/component/llcpp/outgoing_directory.h>

#include <ddktl/device.h>

namespace test_root {

class NumberServer : public fidl::WireServer<fuchsia_composite_test::Device> {
 public:
  explicit NumberServer(uint32_t number) : number_(number) {}

  void GetNumber(GetNumberRequestView request, GetNumberCompleter::Sync& completer) override {
    completer.Reply(number_);
  }

  uint32_t number() { return number_; }

 private:
  uint32_t number_;
};

class TestRoot;
using DeviceType = ddk::Device<TestRoot, ddk::Initializable>;
class TestRoot : public DeviceType {
 public:
  explicit TestRoot(zx_device_t* parent)
      : DeviceType(parent), loop_(&kAsyncLoopConfigNoAttachToCurrentThread) {}
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

  NumberServer server_ = NumberServer(0);
  async::Loop loop_;
  std::optional<component::OutgoingDirectory> outgoing_;
};

}  // namespace test_root

#endif  // SRC_DEVICES_TESTS_COMPOSITE_DRIVER_V1_TEST_ROOT_TEST_ROOT_H_
