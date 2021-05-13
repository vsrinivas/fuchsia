// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include "src/modular/lib/session/session.h"

#include <lib/fdio/directory.h>
#include <lib/fdio/namespace.h>
#include <lib/sys/cpp/testing/fake_launcher.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/vfs/cpp/pseudo_dir.h>
#include <lib/vfs/cpp/service.h>

#include <gtest/gtest.h>

#include "src/lib/files/path.h"
#include "src/lib/fsl/vmo/strings.h"
#include "src/lib/fxl/strings/join_strings.h"
#include "src/lib/fxl/strings/split_string.h"
#include "src/lib/testing/loop_fixture/real_loop_fixture.h"
#include "src/modular/lib/modular_config/modular_config.h"
#include "src/modular/lib/modular_config/modular_config_constants.h"
#include "src/modular/lib/pseudo_dir/pseudo_dir_server.h"
#include "src/modular/lib/session/session_constants.h"

class SessionTest : public gtest::RealLoopFixture {
 public:
  void SetUp() override { ASSERT_EQ(ZX_OK, fdio_ns_get_installed(&ns_)); }

  void TearDown() override {
    for (const auto& path : bound_ns_paths_) {
      ASSERT_EQ(ZX_OK, fdio_ns_unbind(ns_, path.c_str()));
    }
  }

  // Binds the |path| in the current process namespace to directory |handle|.
  void BindNamespacePath(std::string path, zx::handle handle) {
    ASSERT_EQ(ZX_OK, fdio_ns_bind(ns_, path.c_str(), handle.release()));
    bound_ns_paths_.emplace_back(path);
  }

  // Serves a protocol at the given |path|.
  //
  // Can only be called once per test.
  template <typename Interface>
  void ServeProtocolAt(std::string_view path, fidl::InterfaceRequestHandler<Interface> handler) {
    FX_CHECK(!protocol_server_);

    // Split the path into two parts: a path to a directory, and the last segment,
    // an entry in that directory.
    auto path_split = fxl::SplitStringCopy(path, "/", fxl::WhiteSpaceHandling::kKeepWhitespace,
                                           fxl::SplitResult::kSplitWantNonEmpty);
    FX_CHECK(!path_split.empty());

    auto entry_name = std::move(path_split.back());
    path_split.pop_back();
    auto namespace_path = files::JoinPath("/", fxl::JoinStrings(path_split, "/"));

    auto vfs = std::make_unique<vfs::PseudoDir>();
    ASSERT_EQ(ZX_OK, vfs->AddEntry(entry_name, std::make_unique<vfs::Service>(std::move(handler))));

    protocol_server_ = std::make_unique<modular::PseudoDirServer>(std::move(vfs));
    BindNamespacePath(namespace_path, protocol_server_->Serve().Unbind().TakeChannel());
  }

 private:
  fdio_ns_t* ns_ = nullptr;
  std::vector<std::string> bound_ns_paths_;
  std::unique_ptr<modular::PseudoDirServer> protocol_server_;
};

class TestComponentController : fuchsia::sys::ComponentController {
 public:
  TestComponentController() : binding_(this) {}

  void Connect(fidl::InterfaceRequest<fuchsia::sys::ComponentController> request) {
    binding_.Bind(std::move(request));
  }

  void SendOnDirectoryReady() { binding_.events().OnDirectoryReady(); }

  void SendOnTerminated(int64_t exit_code, fuchsia::sys::TerminationReason termination_reason) {
    binding_.events().OnTerminated(exit_code, termination_reason);
  }

 private:
  // |ComponentController|
  void Kill() override { FX_NOTREACHED(); }

  // |ComponentController|
  void Detach() override { FX_NOTREACHED(); }

  fidl::Binding<fuchsia::sys::ComponentController> binding_;
};

class TestBasemgrDebug : fuchsia::modular::internal::BasemgrDebug {
 public:
  TestBasemgrDebug() = default;

  fidl::InterfaceRequestHandler<fuchsia::modular::internal::BasemgrDebug> GetHandler() {
    return [this](fidl::InterfaceRequest<fuchsia::modular::internal::BasemgrDebug> request) {
      bindings_.AddBinding(this, std::move(request));
    };
  }

  // |BasemgrDebug|
  void Shutdown() override {
    is_running_ = false;
    bindings_.CloseAll(ZX_OK);
  }

  // |BasemgrDebug|
  void RestartSession(RestartSessionCallback on_restart_complete) override { FX_NOTREACHED(); }

  // |BasemgrDebug|
  void StartSessionWithRandomId() override { FX_NOTREACHED(); }

  bool is_running() const { return is_running_; }

 private:
  bool is_running_ = true;

  fidl::BindingSet<fuchsia::modular::internal::BasemgrDebug> bindings_;
};

class TestLauncher : fuchsia::modular::session::Launcher {
 public:
  TestLauncher() = default;

  fidl::InterfaceRequestHandler<fuchsia::modular::session::Launcher> GetHandler() {
    return [this](fidl::InterfaceRequest<fuchsia::modular::session::Launcher> request) {
      bindings_.AddBinding(this, std::move(request));
    };
  }

  // |Launcher|
  void LaunchSessionmgr(fuchsia::mem::Buffer config) override {
    // Read the configuration from the buffer.
    std::string config_str;
    if (auto is_read_ok = fsl::StringFromVmo(config, &config_str); !is_read_ok) {
      bindings_.CloseAll(ZX_ERR_INVALID_ARGS);
      return;
    }

    // Parse the configuration.
    auto config_result = modular::ParseConfig(config_str);
    if (config_result.is_error()) {
      bindings_.CloseAll(ZX_ERR_INVALID_ARGS);
      return;
    }

    is_launched_ = true;
    config_ =
        std::make_unique<fuchsia::modular::session::ModularConfig>(config_result.take_value());
  }

  // |Launcher|
  void LaunchSessionmgrWithServices(fuchsia::mem::Buffer config,
                                    fuchsia::sys::ServiceList additional_services) override {
    FX_NOTREACHED();
  }

  bool is_launched() const { return is_launched_; }
  fuchsia::modular::session::ModularConfig* config() const { return config_.get(); }

 private:
  bool is_launched_ = false;
  std::unique_ptr<fuchsia::modular::session::ModularConfig> config_;

  fidl::BindingSet<fuchsia::modular::session::Launcher> bindings_;
};

// Tests that |ConnectToBasemgrDebug| can connect to |BasemgrDebug| served under the hub path
// that exists when basemgr is running as a v1 component.
TEST_F(SessionTest, ConnectToBasemgrDebugV1) {
  static constexpr auto kTestBasemgrDebugPath = "/hub/c/basemgr.cmx/12345/out/debug/basemgr";

  // Serve the |BasemgrDebug| service in the process namespace at the path |kTestBasemgrDebugPath|.
  bool got_request{false};
  fidl::InterfaceRequestHandler<fuchsia::modular::internal::BasemgrDebug> handler =
      [&](fidl::InterfaceRequest<fuchsia::modular::internal::BasemgrDebug> request) {
        got_request = true;
      };
  ServeProtocolAt<fuchsia::modular::internal::BasemgrDebug>(kTestBasemgrDebugPath,
                                                            std::move(handler));

  // Connect to the |BasemgrDebug| service.
  auto result = modular::session::ConnectToBasemgrDebug();
  EXPECT_TRUE(result.is_ok());

  // Ensure that the proxy returned is connected to the instance served above.
  fuchsia::modular::internal::BasemgrDebugPtr basemgr_debug = result.take_value();
  basemgr_debug->StartSessionWithRandomId();

  RunLoopUntil([&]() { return got_request; });
  EXPECT_TRUE(got_request);
}

// Tests that |ConnectToBasemgrDebug| can connect to |BasemgrDebug| served under
// the hub-v2 path that exists when basemgr is running as a session.
TEST_F(SessionTest, ConnectToBasemgrDebugSession) {
  static constexpr auto kTestBasemgrDebugPath =
      "/hub-v2/children/core/children/session-manager/children/session:session/"
      "exec/expose/fuchsia.modular.internal.BasemgrDebug";

  // Serve the |BasemgrDebug| service in the process namespace at the path
  // |kTestBasemgrDebugPath|.
  bool got_request{false};
  fidl::InterfaceRequestHandler<fuchsia::modular::internal::BasemgrDebug> handler =
      [&](fidl::InterfaceRequest<fuchsia::modular::internal::BasemgrDebug> request) {
        got_request = true;
      };
  ServeProtocolAt<fuchsia::modular::internal::BasemgrDebug>(kTestBasemgrDebugPath,
                                                            std::move(handler));

  // Connect to the |BasemgrDebug| service.
  auto result = modular::session::ConnectToBasemgrDebug();
  EXPECT_TRUE(result.is_ok());

  // Ensure that the proxy returned is connected to the instance served above.
  fuchsia::modular::internal::BasemgrDebugPtr basemgr_debug = result.take_value();
  basemgr_debug->StartSessionWithRandomId();

  RunLoopUntil([&]() { return got_request; });
  EXPECT_TRUE(got_request);
}

// Tests that Launch starts basemgr as a v1 component with the fuchsia::sys::Launcher protocol.
TEST_F(SessionTest, Launch) {
  sys::testing::FakeLauncher launcher;

  bool launched{false};
  launcher.RegisterComponent(
      kBasemgrV1Url,
      [&](fuchsia::sys::LaunchInfo launch_info,
          fidl::InterfaceRequest<fuchsia::sys::ComponentController> controller_request) {
        launched = true;

        // Launch must receive the OnDirectoryReady event to return.
        TestComponentController controller;
        controller.Connect(std::move(controller_request));
        controller.SendOnDirectoryReady();
      });

  auto result = RunPromise(modular::session::Launch(&launcher, modular::DefaultConfig()));
  EXPECT_TRUE(result.is_ok());

  EXPECT_TRUE(launched);
}

// Tests that Launch provides basemgr with confiuration in /config_override in its namespace.
TEST_F(SessionTest, LaunchProvidesConfig) {
  // Number of bytes to read from the config file.
  static constexpr auto kReadCount = 1024;

  static constexpr auto kTestSessionLauncherUrl = "fuchsia-pkg://fuchsia.com/test#meta/test.cmx";
  sys::testing::FakeLauncher launcher;

  // Create a ModularConfig to pass to basemgr with some non-default contents.
  auto modular_config = modular::DefaultConfig();
  modular_config.mutable_basemgr_config()->mutable_session_launcher()->set_url(
      kTestSessionLauncherUrl);

  auto expected_config_str = modular::ConfigToJsonString(modular_config);

  // Create an async loop to serve basemgr's namespace directory.
  async::Loop serve_loop{&kAsyncLoopConfigNoAttachToCurrentThread};
  serve_loop.StartThread();

  bool launched{false};
  launcher.RegisterComponent(
      kBasemgrV1Url,
      [&, expected_config_str = std::move(expected_config_str)](
          fuchsia::sys::LaunchInfo launch_info,
          fidl::InterfaceRequest<fuchsia::sys::ComponentController> controller_request) {
        launched = true;

        ASSERT_EQ(1u, launch_info.flat_namespace->paths.size());
        ASSERT_EQ(1u, launch_info.flat_namespace->directories.size());

        // The component should have a /config_override dir in its namespace.
        EXPECT_EQ(modular_config::kOverriddenConfigDir, launch_info.flat_namespace->paths.at(0));

        // Open the startup.config file in the directory.
        auto dir_chan = std::move(launch_info.flat_namespace->directories.at(0));
        fuchsia::io::FileSyncPtr file;
        ASSERT_EQ(ZX_OK, fdio_open_at(dir_chan.release(), modular_config::kStartupConfigFilePath,
                                      fuchsia::io::OPEN_RIGHT_READABLE,
                                      file.NewRequest().TakeChannel().get()));

        // Read from the startup.config file into |config_str|.
        zx_status_t status;
        std::vector<uint8_t> buffer;
        file->Read(kReadCount, &status, &buffer);
        ASSERT_EQ(ZX_OK, status);
        std::string config_str(buffer.size(), 0);
        std::copy(buffer.begin(), buffer.end(), config_str.begin());

        // The config that basemgr received should be the same as the one passed to Launch.
        ASSERT_EQ(expected_config_str, config_str);

        // The thread serving the config PseudoDir must be destroyed before the dir itself.
        serve_loop.Quit();
        serve_loop.JoinThreads();

        // Launch must receive the OnDirectoryReady event to return.
        TestComponentController controller;
        controller.Connect(std::move(controller_request));
        controller.SendOnDirectoryReady();
      });

  auto result = RunPromise(
      modular::session::Launch(&launcher, std::move(modular_config), serve_loop.dispatcher()));
  EXPECT_TRUE(result.is_ok());

  EXPECT_TRUE(launched);
}

// Tests that LaunchSessionmgr can call the Launcher protocol exposed by a session under
// a hub-v2 path with a given config.
TEST_F(SessionTest, LaunchSessionmgr) {
  static constexpr auto kTestLauncherPath =
      "/hub-v2/children/core/children/session-manager/children/session:session/"
      "exec/expose/fuchsia.modular.session.Launcher";

  static constexpr auto kTestSessionLauncherUrl = "fuchsia-pkg://fuchsia.com/test#meta/test.cmx";

  // Serve the |Launcher| protocol in the process namespace at the path |kTestLauncherPath|.
  TestLauncher launcher;
  ServeProtocolAt<fuchsia::modular::session::Launcher>(kTestLauncherPath, launcher.GetHandler());

  // Create a ModularConfig to pass to Launcher with some non-default contents.
  auto modular_config = modular::DefaultConfig();
  modular_config.mutable_basemgr_config()->mutable_session_launcher()->set_url(
      kTestSessionLauncherUrl);

  auto result = modular::session::LaunchSessionmgr(std::move(modular_config));
  EXPECT_TRUE(result.is_ok());

  RunLoopUntil([&]() { return launcher.is_launched(); });

  ASSERT_NE(nullptr, launcher.config());
  EXPECT_EQ(kTestSessionLauncherUrl, launcher.config()->basemgr_config().session_launcher().url());
}

// Tests that Shutdown can shut down basemgr when the |BasemgrDebug| protocol is served
// under the hub path that exists when basemgr is running as a v1 component.
TEST_F(SessionTest, ShutdownV1) {
  static constexpr auto kTestBasemgrDebugPath = "/hub/c/basemgr.cmx/12345/out/debug/basemgr";

  // Serve the |BasemgrDebug| service in the process namespace at the path |kTestBasemgrDebugPath|.
  TestBasemgrDebug basemgr_debug;
  ServeProtocolAt<fuchsia::modular::internal::BasemgrDebug>(kTestBasemgrDebugPath,
                                                            basemgr_debug.GetHandler());

  ASSERT_TRUE(basemgr_debug.is_running());

  auto result = RunPromise(modular::session::Shutdown());
  EXPECT_TRUE(result.is_ok());

  // Ensure that the proxy returned is connected to the instance served above.
  RunLoopUntil([&]() { return !basemgr_debug.is_running(); });
  EXPECT_FALSE(basemgr_debug.is_running());
}

// Tests that Shutdown can shut down basemgr when the |BasemgrDebug| protocol is
// served under the hub-v2 path that exists when basemgr is running as a
// session.
TEST_F(SessionTest, ShutdownSession) {
  static constexpr auto kTestBasemgrDebugPath =
      "/hub-v2/children/core/children/session-manager/children/session:session/"
      "exec/expose/fuchsia.modular.internal.BasemgrDebug";

  // Serve the |BasemgrDebug| service in the process namespace at the path
  // |kTestBasemgrDebugPath|.
  TestBasemgrDebug basemgr_debug;
  ServeProtocolAt<fuchsia::modular::internal::BasemgrDebug>(kTestBasemgrDebugPath,
                                                            basemgr_debug.GetHandler());

  ASSERT_TRUE(basemgr_debug.is_running());

  auto result = RunPromise(modular::session::Shutdown());
  EXPECT_TRUE(result.is_ok());

  // Ensure that the proxy returned is connected to the instance served above.
  RunLoopUntil([&]() { return !basemgr_debug.is_running(); });
  EXPECT_FALSE(basemgr_debug.is_running());
}

// Tests that DeletePersistentConfig invokes basemgr as a v1 component with the
// "delete_persistent_config" argument.
TEST_F(SessionTest, DeletePersistentConfig) {
  sys::testing::FakeLauncher launcher;

  bool launched{false};
  launcher.RegisterComponent(
      kBasemgrV1Url,
      [&](fuchsia::sys::LaunchInfo launch_info,
          fidl::InterfaceRequest<fuchsia::sys::ComponentController> controller_request) {
        launched = true;

        ASSERT_EQ(1u, launch_info.arguments->size());
        EXPECT_EQ("delete_persistent_config", launch_info.arguments->at(0));

        // Launch must receive the OnTerminated event to return.
        TestComponentController controller;
        controller.Connect(std::move(controller_request));
        controller.SendOnTerminated(EXIT_SUCCESS, fuchsia::sys::TerminationReason::EXITED);
      });

  auto result = RunPromise(modular::session::DeletePersistentConfig(&launcher));
  EXPECT_TRUE(result.is_ok());

  EXPECT_TRUE(launched);
}
