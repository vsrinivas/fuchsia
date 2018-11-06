// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/sysmgr/package_updating_loader.h"

#include <memory>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include <fidl/examples/echo/cpp/fidl.h>
#include <fs/service.h>
#include <fuchsia/amber/cpp/fidl.h>
#include <fuchsia/sys/cpp/fidl.h>
#include <lib/fdio/util.h>
#include <lib/zx/channel.h>
#include <lib/zx/time.h>
#include "gtest/gtest.h"
#include "lib/component/cpp/testing/test_with_environment.h"
#include "lib/fidl/cpp/binding_set.h"
#include "lib/fxl/logging.h"

namespace sysmgr {
namespace {

class AmberControlMock : public fuchsia::amber::Control {
 public:
  virtual void GetUpdateComplete(
      ::fidl::StringPtr name, ::fidl::StringPtr version,
      ::fidl::StringPtr merkle,
      GetUpdateCompleteCallback callback) override = 0;

  void AddBinding(fidl::InterfaceRequest<fuchsia::amber::Control> req) {
    bindings_.AddBinding(this, std::move(req));
  }

  //
  // Required stubs.
  //

  void DoTest(int32_t input, DoTestCallback callback) override {
    FXL_LOG(FATAL) << "not implemented";
  }
  void AddSrc(fuchsia::amber::SourceConfig source,
              AddSrcCallback callback) override {
    FXL_LOG(FATAL) << "not implemented";
  }
  void RemoveSrc(::fidl::StringPtr id, RemoveSrcCallback callback) override {
    FXL_LOG(FATAL) << "not implemented";
  }
  void ListSrcs(ListSrcsCallback callback) override {
    FXL_LOG(FATAL) << "not implemented";
  }
  void GetBlob(::fidl::StringPtr merkle) override {
    FXL_LOG(FATAL) << "not implemented";
  }
  void PackagesActivated(::fidl::VectorPtr<::fidl::StringPtr> merkle) override {
    FXL_LOG(FATAL) << "not implemented";
  }
  void CheckForSystemUpdate(
      fuchsia::amber::Control::CheckForSystemUpdateCallback callback) override {
    FXL_LOG(FATAL) << "not implemented";
  }
  void Login(::fidl::StringPtr sourceId, LoginCallback callback) override {
    FXL_LOG(FATAL) << "not implemented";
  }
  void SetSrcEnabled(
      ::fidl::StringPtr id, bool enabled,
      fuchsia::amber::Control::SetSrcEnabledCallback callback) override {
    FXL_LOG(FATAL) << "not implemented";
  }
  void GC() override {
    FXL_LOG(FATAL) << "not implemented";
  }

 private:
  fidl::BindingSet<fuchsia::amber::Control> bindings_;
};

constexpr char kRealm[] = "package_updating_loader_env";

class PackageUpdatingLoaderTest
    : public component::testing::TestWithEnvironment {
 protected:
  void Init(AmberControlMock* amber_service) {
    fuchsia::amber::ControlPtr amber_ctl;
    amber_service->AddBinding(amber_ctl.NewRequest(dispatcher()));
    loader_ = std::make_unique<PackageUpdatingLoader>(
        "my_amber", std::move(amber_ctl), dispatcher());
    loader_service_ =
        fbl::AdoptRef(new fs::Service([this](zx::channel channel) {
          loader_->AddBinding(
              fidl::InterfaceRequest<fuchsia::sys::Loader>(std::move(channel)));
          return ZX_OK;
        }));
    auto services = CreateServicesWithCustomLoader(loader_service_);
    env_ = CreateNewEnclosingEnvironment(kRealm, std::move(services));
  }

  fuchsia::sys::LaunchInfo CreateLaunchInfo(const std::string& url,
                                            zx::channel dir) {
    fuchsia::sys::LaunchInfo launch_info;
    launch_info.url = url;
    launch_info.directory_request = std::move(dir);
    return launch_info;
  }

  template <typename RequestType>
  void ConnectToServiceAt(zx::channel dir,
                          fidl::InterfaceRequest<RequestType> req) {
    ASSERT_EQ(ZX_OK, fdio_service_connect_at(dir.release(), RequestType::Name_,
                                             req.TakeChannel().release()));
  }

  std::unique_ptr<component::testing::EnclosingEnvironment> env_;

 private:
  std::unique_ptr<PackageUpdatingLoader> loader_;
  fbl::RefPtr<fs::Service> loader_service_;
};

class AmberControlSuccessMock : public AmberControlMock {
 public:
  void GetUpdateComplete(::fidl::StringPtr name, ::fidl::StringPtr version,
                         ::fidl::StringPtr merkle,
                         GetUpdateCompleteCallback callback) override {
    get_update_args_ = std::make_tuple(name.get(), version.get(), merkle.get());
    zx::channel h1, h2;
    ASSERT_EQ(ZX_OK, zx::channel::create(0, &h1, &h2));
    callback(std::move(h2));
    // Simulate update delay.
    zx::nanosleep(zx::deadline_after(zx::sec(2)));
    static const char kMessage[] = "Hello world";
    ASSERT_EQ(ZX_OK, h1.write(0, kMessage, sizeof(kMessage), nullptr, 0));
    update_channels_.push_back(std::move(h1));
  }

  const std::tuple<std::string, std::string, std::string> get_update_args()
      const {
    return get_update_args_;
  }

 private:
  std::tuple<std::string, std::string, std::string> get_update_args_;
  std::vector<zx::channel> update_channels_;
};


TEST_F(PackageUpdatingLoaderTest, Success) {
  AmberControlSuccessMock amber_service;
  Init(&amber_service);

  // Launch a component in the environment, and prove it started successfully
  // by trying to use a service offered by it.
  zx::channel h1, h2;
  ASSERT_EQ(ZX_OK, zx::channel::create(0, &h1, &h2));
  auto launch_info =
      CreateLaunchInfo("echo2_server_cpp", std::move(h2));
  auto controller = env_->CreateComponent(std::move(launch_info));
  fidl::examples::echo::EchoPtr echo;
  ConnectToServiceAt(std::move(h1), echo.NewRequest());
  const std::string message = "component launched";
  fidl::StringPtr ret_msg = "";
  echo->EchoString(message, [&](fidl::StringPtr retval) { ret_msg = retval; });
  ASSERT_TRUE(RunLoopWithTimeoutOrUntil(
      [&] { return std::string(ret_msg) == message; }, zx::sec(10)));

  // Verify that GetUpdateComplete was called with the expected arguments.
  EXPECT_EQ(amber_service.get_update_args(),
            std::make_tuple(std::string("echo2_server_cpp"), std::string("0"),
                            std::string("")));
}

class AmberControlDaemonErrorMock : public AmberControlMock {
 public:
  void GetUpdateComplete(::fidl::StringPtr name, ::fidl::StringPtr version,
                         ::fidl::StringPtr merkle,
                         GetUpdateCompleteCallback callback) override {
    zx::channel h1, h2;
    ASSERT_EQ(ZX_OK, zx::channel::create(0, &h1, &h2));
    callback(std::move(h2));
    // Simulate update delay.
    zx::nanosleep(zx::deadline_after(zx::sec(2)));
    // ZXSIO_DAEMON_ERROR
    h1.signal_peer(0, ZX_USER_SIGNAL_0);
    static const char kMessage[] = "Update failed";
    ASSERT_EQ(ZX_OK, h1.write(0, kMessage, sizeof(kMessage), nullptr, 0));
    update_channels_.push_back(std::move(h1));
  }

 private:
  std::vector<zx::channel> update_channels_;
};

TEST_F(PackageUpdatingLoaderTest, DaemonError) {
  AmberControlDaemonErrorMock amber_service;
  Init(&amber_service);

  // Launch a component in the environment, and prove it started successfully
  // by trying to use a service offered by it.
  zx::channel h1, h2;
  ASSERT_EQ(ZX_OK, zx::channel::create(0, &h1, &h2));
  auto launch_info =
      CreateLaunchInfo("echo2_server_cpp", std::move(h2));
  auto controller = env_->CreateComponent(std::move(launch_info));
  fidl::examples::echo::EchoPtr echo;
  ConnectToServiceAt(std::move(h1), echo.NewRequest());
  const std::string message = "component launched";
  fidl::StringPtr ret_msg = "";
  echo->EchoString(message, [&](fidl::StringPtr retval) { ret_msg = retval; });
  // Even though the update failed, the loader should load the component anyway.
  ASSERT_TRUE(RunLoopWithTimeoutOrUntil(
      [&] { return std::string(ret_msg) == message; }, zx::sec(10)));
}

class AmberControlPeerClosedMock : public AmberControlMock {
 public:
  void GetUpdateComplete(::fidl::StringPtr name, ::fidl::StringPtr version,
                         ::fidl::StringPtr merkle,
                         GetUpdateCompleteCallback callback) override {
    zx::channel h1, h2;
    ASSERT_EQ(ZX_OK, zx::channel::create(0, &h1, &h2));
    callback(std::move(h2));
    // Simulate update delay.
    zx::nanosleep(zx::deadline_after(zx::sec(2)));
    // Close channel before sending a message.
  }
};

TEST_F(PackageUpdatingLoaderTest, PeerClosed) {
  AmberControlPeerClosedMock amber_service;
  Init(&amber_service);

  // Launch a component in the environment, and prove it started successfully
  // by trying to use a service offered by it.
  zx::channel h1, h2;
  ASSERT_EQ(ZX_OK, zx::channel::create(0, &h1, &h2));
  auto launch_info =
      CreateLaunchInfo("echo2_server_cpp", std::move(h2));
  auto controller = env_->CreateComponent(std::move(launch_info));
  bool terminated = false;
  fuchsia::sys::TerminationReason reason;
  controller.events().OnTerminated =
      [&](int64_t code, fuchsia::sys::TerminationReason r) {
        terminated = true;
        reason = r;
      };
  ASSERT_TRUE(
      RunLoopWithTimeoutOrUntil([&] { return terminated; }, zx::sec(10)));
  EXPECT_EQ(reason, fuchsia::sys::TerminationReason::PACKAGE_NOT_FOUND);
}

}  // namespace
}  // namespace sysmgr
