// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_APPMGR_INTEGRATION_TESTS_MOCK_RUNNER_MOCK_RUNNER_H_
#define GARNET_BIN_APPMGR_INTEGRATION_TESTS_MOCK_RUNNER_MOCK_RUNNER_H_

#include <unordered_map>

#include <fuchsia/sys/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <test/component/mockrunner/cpp/fidl.h>

#include "lib/component/cpp/startup_context.h"
#include "lib/fidl/cpp/binding.h"
#include "lib/fxl/macros.h"

namespace component {
namespace testing {

namespace mockrunner = test::component::mockrunner;

class MockRunner;

class FakeSubComponent : public fuchsia::sys::ComponentController {
 public:
  FakeSubComponent(
      uint64_t id, fuchsia::sys::Package application,
      fuchsia::sys::StartupInfo startup_info,
      ::fidl::InterfaceRequest<fuchsia::sys::ComponentController> controller,
      MockRunner* runner);
  ~FakeSubComponent() override;

  void Kill() override;
  void Detach() override;
  void Wait(WaitCallback callback) override {
    wait_callbacks_.push_back(std::move(callback));
    SendReturnCodeIfTerminated();
  }

  void SendReturnCodeIfTerminated();

 private:
  uint64_t id_;
  uint64_t return_code_;
  bool alive_;
  fidl::Binding<fuchsia::sys::ComponentController> binding_;
  std::vector<WaitCallback> wait_callbacks_;
  MockRunner* runner_;

  FXL_DISALLOW_COPY_AND_ASSIGN(FakeSubComponent);
};

class MockRunner : public fuchsia::sys::Runner, public mockrunner::MockRunner {
 public:
  MockRunner();
  ~MockRunner() override;

  void Kill() override;

  void KillComponent(uint64_t id, uint64_t errorcode) override;

  void Start() { loop_.Run(); }

  std::unique_ptr<FakeSubComponent> ExtractComponent(uint64_t id);

 private:
  // |fuchsia::sys::Runner|
  void StartComponent(
      fuchsia::sys::Package application, fuchsia::sys::StartupInfo startup_info,
      ::fidl::InterfaceRequest<fuchsia::sys::ComponentController> controller)
      override;

  async::Loop loop_;
  std::unique_ptr<component::StartupContext> context_;
  fidl::BindingSet<fuchsia::sys::Runner> bindings_;
  fidl::Binding<mockrunner::MockRunner> mock_binding_;
  uint64_t component_id_counter_;
  std::unordered_map<uint64_t, std::unique_ptr<FakeSubComponent>> components_;

  FXL_DISALLOW_COPY_AND_ASSIGN(MockRunner);
};

}  // namespace testing
}  // namespace component

#endif  // GARNET_BIN_APPMGR_INTEGRATION_TESTS_MOCK_RUNNER_MOCK_RUNNER_H_
