// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_APPMGR_INTEGRATION_TESTS_MOCK_RUNNER_MOCK_RUNNER_H_
#define GARNET_BIN_APPMGR_INTEGRATION_TESTS_MOCK_RUNNER_MOCK_RUNNER_H_

#include <fuchsia/sys/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/sys/cpp/outgoing_directory.h>
#include <lib/sys/cpp/service_directory.h>
#include <src/lib/fxl/macros.h>
#include <test/component/mockrunner/cpp/fidl.h>
#include <unordered_map>

namespace component {
namespace testing {

namespace mockrunner = test::component::mockrunner;
using fuchsia::sys::TerminationReason;

class MockRunner;

class FakeSubComponent : public fuchsia::sys::ComponentController,
                         public mockrunner::MockComponent {
 public:
  FakeSubComponent(
      uint64_t id, fuchsia::sys::Package application,
      fuchsia::sys::StartupInfo startup_info,
      ::fidl::InterfaceRequest<fuchsia::sys::ComponentController> controller,
      MockRunner* runner);
  ~FakeSubComponent() override;

  // fuchsia::sys::ComponentController
  void Kill() override;
  void Detach() override;

  void SetReturnCode(int64_t code) { return_code_ = code; }

  // mockrunner::MockComponent
  void Kill(uint64_t error_code) override {
    SetReturnCode(error_code);
    Kill();
  }

  void ConnectToService(::std::string service_name,
                        zx::channel channel) override {
    svc_->Connect(service_name, std::move(channel));
  }

  void SetServiceDirectory(zx::channel channel) override {
    service_dir_.reset(channel.release());
  }

  void PublishService(::std::string service_name,
                      PublishServiceCallback callback) override;

  void SendReturnCodeIfTerminated();

  void AddMockControllerBinding(
      ::fidl::InterfaceRequest<mockrunner::MockComponent> req) {
    mock_bindings_.AddBinding(this, std::move(req));
  }

 private:
  uint64_t id_;
  uint64_t return_code_;
  bool alive_;
  std::shared_ptr<sys::ServiceDirectory> svc_;
  zx::channel service_dir_;
  fidl::Binding<fuchsia::sys::ComponentController> binding_;
  fidl::BindingSet<mockrunner::MockComponent> mock_bindings_;
  MockRunner* runner_;
  sys::OutgoingDirectory outgoing_;

  FXL_DISALLOW_COPY_AND_ASSIGN(FakeSubComponent);
};

class MockRunner : public fuchsia::sys::Runner, public mockrunner::MockRunner {
 public:
  MockRunner();
  ~MockRunner() override;

  void Crash() override;

  void ConnectToComponent(
      uint64_t id,
      ::fidl::InterfaceRequest<mockrunner::MockComponent> req) override;

  void Start() { loop_.Run(); }

  std::unique_ptr<FakeSubComponent> ExtractComponent(uint64_t id);

 private:
  // |fuchsia::sys::Runner|
  void StartComponent(
      fuchsia::sys::Package application, fuchsia::sys::StartupInfo startup_info,
      ::fidl::InterfaceRequest<fuchsia::sys::ComponentController> controller)
      override;

  async::Loop loop_;
  std::unique_ptr<sys::ComponentContext> context_;
  fidl::BindingSet<fuchsia::sys::Runner> bindings_;
  fidl::Binding<mockrunner::MockRunner> mock_binding_;
  uint64_t component_id_counter_;
  std::unordered_map<uint64_t, std::unique_ptr<FakeSubComponent>> components_;

  FXL_DISALLOW_COPY_AND_ASSIGN(MockRunner);
};

}  // namespace testing
}  // namespace component

#endif  // GARNET_BIN_APPMGR_INTEGRATION_TESTS_MOCK_RUNNER_MOCK_RUNNER_H_
