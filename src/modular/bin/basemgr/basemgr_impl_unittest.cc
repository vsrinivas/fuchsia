// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/modular/bin/basemgr/basemgr_impl.h"

#include <fuchsia/examples/cpp/fidl.h>
#include <fuchsia/modular/internal/cpp/fidl_test_base.h>
#include <fuchsia/testing/modular/cpp/fidl.h>
#include <lib/gtest/real_loop_fixture.h>
#include <lib/sys/cpp/testing/fake_component.h>
#include <lib/sys/cpp/testing/fake_launcher.h>

#include <utility>

#include "src/lib/files/file.h"
#include "src/lib/fsl/io/fd.h"
#include "src/lib/fsl/vmo/strings.h"
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
                async_dispatcher_t* dispatcher = nullptr) {
    fake_launcher.RegisterComponent(
        std::move(url),
        [this, dispatcher](fuchsia::sys::LaunchInfo launch_info,
                           fidl::InterfaceRequest<fuchsia::sys::ComponentController> ctrl) {
          ctrls_.push_back(std::move(ctrl));
          zx_status_t status =
              directory_.Serve(fuchsia::io::OPEN_RIGHT_READABLE | fuchsia::io::OPEN_RIGHT_WRITABLE,
                               std::move(launch_info.directory_request), dispatcher);
          ZX_ASSERT(status == ZX_OK);

          namespace_map_.clear();
          for (size_t i = 0; i < launch_info.flat_namespace->paths.size(); ++i) {
            namespace_map_.emplace(launch_info.flat_namespace->paths[i],
                                   std::move(launch_info.flat_namespace->directories[i]));
          }
          launch_count_++;
        });
  }

  int launch_count() const { return launch_count_; }
  NamespaceMap& namespace_map() { return namespace_map_; }

 private:
  int launch_count_ = 0;
  vfs::PseudoDir directory_;
  std::vector<fidl::InterfaceRequest<fuchsia::sys::ComponentController>> ctrls_;
  NamespaceMap namespace_map_;
};

class FakeSessionmgr : public fuchsia::modular::internal::testing::Sessionmgr_TestBase {
 public:
  FakeSessionmgr() { component_.AddPublicService(bindings_.GetHandler(this)); }

  void NotImplemented_(const std::string& name) override {}

  void Initialize(
      std::string session_id,
      fidl::InterfaceHandle<::fuchsia::modular::internal::SessionContext> session_context,
      fuchsia::sys::ServiceList additional_services_for_agents,
      fuchsia::ui::views::ViewToken view_token, fuchsia::ui::views::ViewRefControl control_ref,
      fuchsia::ui::views::ViewRef view_ref) override {
    additional_services_for_agents_ = std::move(additional_services_for_agents);
    initialized_ = true;
  }

  FakeComponentWithNamespace* component() { return &component_; }
  bool initialized() const { return initialized_; }
  std::optional<fuchsia::sys::ServiceList>& additional_services_for_agents() {
    return additional_services_for_agents_;
  }

 private:
  bool initialized_ = false;
  std::optional<fuchsia::sys::ServiceList> additional_services_for_agents_ = std::nullopt;
  fidl::BindingSet<fuchsia::modular::internal::Sessionmgr> bindings_;
  FakeComponentWithNamespace component_;
};

class BasemgrImplTest : public gtest::RealLoopFixture {
 public:
  BasemgrImplTest() : basemgr_inspector_(&inspector) {}

  void SetUp() override {}

  void CreateBasemgrImpl(fuchsia::modular::session::ModularConfig config) {
    basemgr_impl_ =
        std::make_unique<BasemgrImpl>(ModularConfigAccessor(std::move(config)), outgoing_directory_,
                                      &basemgr_inspector_, GetLauncher(), std::move(presenter_),
                                      std::move(device_administrator_), std::move(on_shutdown_));
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
  inspect::Inspector inspector;
  BasemgrInspector basemgr_inspector_;
  fuchsia::ui::policy::PresenterPtr presenter_;
  fuchsia::hardware::power::statecontrol::AdminPtr device_administrator_;
  sys::testing::FakeLauncher fake_launcher_;
  std::unique_ptr<BasemgrImpl> basemgr_impl_;
};

// Tests that basemgr starts a session with the given configuration when instructed by
// the session launcher component.
TEST_F(BasemgrImplTest, StartsSessionWithConfig) {
  static constexpr auto kTestSessionShellUrl =
      "fuchsia-pkg://fuchsia.com/test_session_shell#meta/test_session_shell.cmx";

  FakeSessionmgr sessionmgr{};
  sessionmgr.component()->Register(modular_config::kSessionmgrUrl, fake_launcher_);

  CreateBasemgrImpl(DefaultConfig());

  // Create the configuration that the session launcher component passes to basemgr.
  fuchsia::modular::session::SessionShellMapEntry entry;
  entry.mutable_config()->mutable_app_config()->set_url(kTestSessionShellUrl);
  entry.mutable_config()->mutable_app_config()->set_args({});
  fuchsia::modular::session::ModularConfig config;
  config.mutable_basemgr_config()->mutable_session_shell_map()->push_back(std::move(entry));

  auto config_json = modular::ConfigToJsonString(config);
  auto config_buf = BufferFromString(config_json);

  // Launch the session
  auto session_launcher = GetSessionLauncher();
  session_launcher->LaunchSessionmgr(std::move(config_buf));

  // sessionmgr should be started and initialized.
  RunLoopUntil([&]() { return sessionmgr.initialized(); });

  // sessionmgr's namespace should contain the config file at /config_override/data/startup.config.
  auto config_dir_it =
      sessionmgr.component()->namespace_map().find(modular_config::kOverriddenConfigDir);
  ASSERT_TRUE(config_dir_it != sessionmgr.component()->namespace_map().end());

  ASSERT_EQ(ZX_OK, loop().StartThread());
  async::TaskClosure task([&] {
    auto dir_fd = fsl::OpenChannelAsFileDescriptor(config_dir_it->second.TakeChannel());

    std::string config_contents;
    ASSERT_TRUE(files::ReadFileToStringAt(dir_fd.get(), modular_config::kStartupConfigFilePath,
                                          &config_contents));

    EXPECT_EQ(config_json, config_contents);

    basemgr_impl_->Terminate();
  });
  task.Post(dispatcher());

  RunLoopUntil([&]() { return did_shut_down_; });
}

// Tests that basemgr can proxy a FIDL service from a CFv2 component to
// CFv1 sessionmgr via its |additional_services_for_agents|.
TEST_F(BasemgrImplTest, EchoServerIsUsed) {
  FakeSessionmgr sessionmgr{};
  sessionmgr.component()->Register(modular_config::kSessionmgrUrl, fake_launcher_);

  CreateBasemgrImpl(DefaultConfig());

  auto config_buf = BufferFromString(modular::ConfigToJsonString(DefaultConfig()));

  // Launch the session
  auto session_launcher = GetSessionLauncher();
  session_launcher->LaunchSessionmgr(std::move(config_buf));

  // sessionmgr should be started and initialized.
  RunLoopUntil([&]() { return sessionmgr.initialized(); });

  // sessionmgr should have received the service in
  // |additional_services_for_agents|
  auto& services = sessionmgr.additional_services_for_agents().value();
  ASSERT_EQ(1u, services.names.size());

  // Connect to a service that was designated in this test component's CML as a
  // "svc_for_v1_sessionmgr", and made available via sessionmgr's
  // |additional_services_for_agents|.
  //
  // This test uses one of the fuchsia.git example Echo services.
  //
  // NOTE: Beware, there are multiple echo service implementations, and the FIDL
  // and component paths vary. Make sure all fully-qualified names of both the
  // FIDL service protocol and the component are consistent across this test
  // component's BUILD.gn, CML, #includes, and C++ namespaces and identifiers.
  sys::ServiceDirectory service_dir{std::move(services.host_directory)};
  auto echo = service_dir.Connect<fuchsia::examples::Echo>();
  zx_status_t status = ZX_OK;
  echo.set_error_handler([&](zx_status_t error) { status = error; });

  // NOTE: Be careful not to exceed the fidl-declared MAX_STRING_LENGTH for the
  // message parameter.
  const std::string message = "hello from echo... echo...";

  std::string ret_msg;
  echo->EchoString(message, [&](fidl::StringPtr retval) { ret_msg = retval.value_or(""); });

  RunLoopUntil([&] { return ret_msg == message || status != ZX_OK; });

  if (status != ZX_OK) {
    FX_PLOGS(FATAL, status) << "FIDL request failed";
  }

  basemgr_impl_->Terminate();
  RunLoopUntil([&]() { return did_shut_down_; });
}

// Tests that LaunchSessionmgr closes the channel with an ZX_ERR_INVALID_ARGS epitaph if the
// config buffer is not readable.
TEST_F(BasemgrImplTest, LaunchSessionmgrFailsGivenUnreadableBuffer) {
  FakeSessionmgr sessionmgr{};
  sessionmgr.component()->Register(modular_config::kSessionmgrUrl, fake_launcher_);

  CreateBasemgrImpl(DefaultConfig());

  // Create a configuration Buffer that has an incorrect size.
  auto config_buf = BufferFromString("");
  config_buf.size = 1;

  // Connect to Launcher with a handler that lets us capture the error.
  auto session_launcher = GetSessionLauncher();

  bool error_handler_called{false};
  zx_status_t error_status{ZX_OK};
  session_launcher.set_error_handler([&](zx_status_t status) {
    error_handler_called = true;
    error_status = status;
  });

  session_launcher->LaunchSessionmgr(std::move(config_buf));

  RunLoopUntil([&] { return error_handler_called; });
  EXPECT_EQ(ZX_ERR_INVALID_ARGS, error_status);

  basemgr_impl_->Terminate();
  RunLoopUntil([&]() { return did_shut_down_; });
}

// Tests that LaunchSessionmgr closes the channel with an ZX_ERR_INVALID_ARGS epitaph if the
// config buffer does not contain valid Modular configuration JSON.
TEST_F(BasemgrImplTest, LaunchSessionmgrFailsGivenInvalidConfigJson) {
  FakeSessionmgr sessionmgr{};
  sessionmgr.component()->Register(modular_config::kSessionmgrUrl, fake_launcher_);

  CreateBasemgrImpl(DefaultConfig());

  // Create a configuration that is invalid JSON.
  auto config_buf = BufferFromString("this is not valid json");

  // Connect to Launcher with a handler that lets us capture the error.
  auto session_launcher = GetSessionLauncher();

  bool error_handler_called{false};
  zx_status_t error_status{ZX_OK};
  session_launcher.set_error_handler([&](zx_status_t status) {
    error_handler_called = true;
    error_status = status;
  });

  session_launcher->LaunchSessionmgr(std::move(config_buf));

  RunLoopUntil([&] { return error_handler_called; });
  EXPECT_EQ(ZX_ERR_INVALID_ARGS, error_status);

  basemgr_impl_->Terminate();
  RunLoopUntil([&]() { return did_shut_down_; });
}

// Tests that basemgr starts a new sessionmgr component with a new configuration when instructed
// to launch a new session.
TEST_F(BasemgrImplTest, LaunchSessionmgrReplacesExistingSession) {
  FakeSessionmgr sessionmgr{};
  sessionmgr.component()->Register(modular_config::kSessionmgrUrl, fake_launcher_);

  CreateBasemgrImpl(DefaultConfig());

  auto config_buf = BufferFromString(modular::ConfigToJsonString(DefaultConfig()));

  // Launch the session
  auto session_launcher = GetSessionLauncher();
  session_launcher->LaunchSessionmgr(std::move(config_buf));

  // sessionmgr should be started and initialized.
  RunLoopUntil([&]() { return sessionmgr.initialized(); });

  EXPECT_EQ(1, sessionmgr.component()->launch_count());

  // Launch the session again
  config_buf = BufferFromString(modular::ConfigToJsonString(DefaultConfig()));
  session_launcher->LaunchSessionmgr(std::move(config_buf));

  RunLoopUntil([&] { return sessionmgr.component()->launch_count() == 2; });
}

}  // namespace modular
