// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MODULAR_BIN_BASEMGR_BASEMGR_IMPL_TEST_FIXTURE_H_
#define SRC_MODULAR_BIN_BASEMGR_BASEMGR_IMPL_TEST_FIXTURE_H_

#include <fuchsia/modular/cpp/fidl_test_base.h>
#include <fuchsia/modular/internal/cpp/fidl_test_base.h>
#include <lib/sys/cpp/testing/fake_component.h>
#include <lib/sys/cpp/testing/fake_launcher.h>

#include "src/lib/fsl/vmo/strings.h"
#include "src/lib/testing/loop_fixture/real_loop_fixture.h"
#include "src/modular/bin/basemgr/basemgr_impl.h"
#include "src/modular/lib/modular_config/modular_config.h"
#include "src/modular/lib/modular_config/modular_config_accessor.h"
#include "src/modular/lib/modular_config/modular_config_constants.h"
#include "src/modular/lib/pseudo_dir/pseudo_dir_server.h"

namespace modular {

class FakeComponentWithNamespace {
 public:
  using NamespaceMap = std::map<std::string, fidl::InterfaceHandle<fuchsia::io::Directory>>;

  FakeComponentWithNamespace() = default;

  // Adds specified interface to the set of public interfaces.
  //
  // Adds a supported service with the given |service_name|, using the given
  // |interface_request_handler|, which should remain valid for the lifetime of
  // this object.
  //
  // A typical usage may be:
  //
  //   AddPublicService(foobar_bindings_.GetHandler(this));
  template <typename Interface>
  zx_status_t AddPublicService(fidl::InterfaceRequestHandler<Interface> handler,
                               const std::string& service_name = Interface::Name_) {
    return directory_.AddEntry(service_name, std::make_unique<vfs::Service>(std::move(handler)));
  }

  // Registers this component with a FakeLauncher.
  void Register(std::string url, sys::testing::FakeLauncher& fake_launcher,
                async_dispatcher_t* dispatcher = nullptr);

  // Closes all ComponentController handles that have been stored as a result
  // of this fake component being started.
  void CloseAllComponentControllerHandles() { ctrls_.clear(); }

  int launch_count() const { return launch_count_; }
  NamespaceMap& namespace_map() { return namespace_map_; }

 private:
  int launch_count_ = 0;
  vfs::PseudoDir directory_;
  std::vector<fidl::InterfaceRequest<fuchsia::sys::ComponentController>> ctrls_;
  NamespaceMap namespace_map_;
};

class FakeSessionmgr : public fuchsia::modular::internal::testing::Sessionmgr_TestBase,
                       public fuchsia::modular::testing::Lifecycle_TestBase {
 public:
  // Providing `on_shutdown` requires the caller to call
  // component()->CloseAllComponentControllerHandles() after `on_shutdown` is called in order to
  // avoid basemgr's timeout on waiting for FakeSessionmgr to terminate.
  explicit FakeSessionmgr(sys::testing::FakeLauncher& launcher,
                          std::function<void()> on_shutdown = nullptr)
      : on_shutdown_(std::move(on_shutdown)) {
    component_.AddPublicService(sessionmgr_bindings_.GetHandler(this));
    component_.AddPublicService(lifecycle_bindings_.GetHandler(this));
    component_.Register(modular_config::kSessionmgrUrl, launcher);
  }

  void NotImplemented_(const std::string& name) override {}

  void Initialize(std::string session_id,
                  fidl::InterfaceHandle<fuchsia::modular::internal::SessionContext> session_context,
                  fuchsia::sys::ServiceList v2_services_for_sessionmgr,
                  fidl::InterfaceRequest<fuchsia::io::Directory> svc_from_v1_sessionmgr,
                  fuchsia::ui::views::ViewCreationToken view_creation_token) override {
    v2_services_for_sessionmgr_ = std::move(v2_services_for_sessionmgr);
    initialized_ = true;
  }

  void InitializeLegacy(
      std::string session_id,
      fidl::InterfaceHandle<fuchsia::modular::internal::SessionContext> session_context,
      fuchsia::sys::ServiceList v2_services_for_sessionmgr,
      fidl::InterfaceRequest<fuchsia::io::Directory> svc_from_v1_sessionmgr,
      fuchsia::ui::views::ViewToken view_token, fuchsia::ui::views::ViewRefControl control_ref,
      fuchsia::ui::views::ViewRef view_ref) override {
    v2_services_for_sessionmgr_ = std::move(v2_services_for_sessionmgr);
    initialized_ = true;
  }

  void InitializeWithoutView(
      std::string session_id,
      fidl::InterfaceHandle<fuchsia::modular::internal::SessionContext> session_context,
      fuchsia::sys::ServiceList v2_services_for_sessionmgr,
      fidl::InterfaceRequest<fuchsia::io::Directory> svc_from_v1_sessionmgr) override {
    v2_services_for_sessionmgr_ = std::move(v2_services_for_sessionmgr);
    initialized_ = true;
  }

  // fuchsia.modular.Lifecycle
  void Terminate() override {
    if (!!on_shutdown_) {
      on_shutdown_();
    } else {
      // Default implementation that conforms to modular::Lifecycle's contract.
      component_.CloseAllComponentControllerHandles();
    }
  }

  FakeComponentWithNamespace* component() { return &component_; }
  bool initialized() const { return initialized_; }
  std::optional<fuchsia::sys::ServiceList>& v2_services_for_sessionmgr() {
    return v2_services_for_sessionmgr_;
  }

 private:
  bool initialized_ = false;
  std::function<void()> on_shutdown_;
  std::optional<fuchsia::sys::ServiceList> v2_services_for_sessionmgr_ = std::nullopt;
  fidl::BindingSet<fuchsia::modular::internal::Sessionmgr> sessionmgr_bindings_;
  fidl::BindingSet<fuchsia::modular::Lifecycle> lifecycle_bindings_;
  FakeComponentWithNamespace component_;
};

class BasemgrImplTestFixture : public gtest::RealLoopFixture {
 public:
  void SetUp() override {}

  void CreateBasemgrImpl(fuchsia::modular::session::ModularConfig config) {
    basemgr_impl_ = std::make_unique<BasemgrImpl>(
        ModularConfigAccessor(std::move(config)), outgoing_directory_, false, GetLauncher(),
        std::move(presenter_), std::move(device_administrator_),
        /*session_restarter_=*/nullptr,
        /*child_listener=*/nullptr, std::move(view_provider_), std::move(on_shutdown_));
  }

  fuchsia::modular::session::LauncherPtr GetSessionLauncher() {
    fuchsia::modular::session::LauncherPtr session_launcher;
    basemgr_impl_->GetLauncherHandler()(session_launcher.NewRequest());
    return session_launcher;
  }

 protected:
  fuchsia::sys::LauncherPtr GetLauncher() {
    fuchsia::sys::LauncherPtr launcher;
    fake_launcher_.GetHandler()(launcher.NewRequest());
    return launcher;
  }

  static fuchsia::mem::Buffer BufferFromString(std::string_view contents) {
    fuchsia::mem::Buffer config_buf;
    ZX_ASSERT(fsl::VmoFromString(contents, &config_buf));
    return config_buf;
  }

  bool did_shut_down_ = false;
  fit::function<void()> on_shutdown_ = [&]() { did_shut_down_ = true; };
  std::shared_ptr<sys::OutgoingDirectory> outgoing_directory_ =
      std::make_shared<sys::OutgoingDirectory>();
  fuchsia::ui::policy::PresenterPtr presenter_;
  fuchsia::hardware::power::statecontrol::AdminPtr device_administrator_;
  fuchsia::ui::app::ViewProviderPtr view_provider_;
  sys::testing::FakeLauncher fake_launcher_;
  std::unique_ptr<BasemgrImpl> basemgr_impl_;
};

}  // namespace modular

#endif  // SRC_MODULAR_BIN_BASEMGR_BASEMGR_IMPL_TEST_FIXTURE_H_
