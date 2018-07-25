// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/appmgr/component_controller_impl.h"
#include "garnet/bin/appmgr/realm.h"

#include <stdint.h>
#include <stdio.h>
#include <unistd.h>

#include <fidl/examples/echo/cpp/fidl.h>
#include <fs/pseudo-dir.h>
#include <fs/synchronous-vfs.h>
#include <fuchsia/sys/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async/default.h>
#include <lib/fdio/util.h>

#include "garnet/bin/appmgr/util.h"
#include "lib/component/cpp/environment_services.h"
#include "lib/component/cpp/testing/test_util.h"
#include "lib/component/cpp/testing/test_with_environment.h"
#include "lib/fidl/cpp/binding_set.h"
#include "lib/fxl/files/scoped_temp_dir.h"

namespace component {
namespace {

using fuchsia::sys::TerminationReason;

using testing::CloneFileDescriptor;
using testing::EnclosingEnvironment;
using testing::TestWithEnvironment;

class RealmTest : public TestWithEnvironment {
 protected:
  void SetUp() override {
    TestWithEnvironment::SetUp();
    std::string tmp_file;
    ASSERT_TRUE(tmp_dir_.NewTempFile(&tmp_file));
    outf_ = std::fopen(tmp_file.c_str(), "w");
  }

  fuchsia::sys::LaunchInfo CreateLaunchInfo(const std::string& url) {
    fuchsia::sys::LaunchInfo launch_info;
    launch_info.url = url;
    int out_fd = fileno(outf_);

    launch_info.out = CloneFileDescriptor(out_fd);
    return launch_info;
  }

  fuchsia::sys::ComponentControllerPtr RunComponent(
      EnclosingEnvironment* enclosing_environment, const std::string& url) {
    return enclosing_environment->CreateComponent(CreateLaunchInfo(url));
  }

 private:
  files::ScopedTempDir tmp_dir_;
  FILE* outf_;
};

const char kRealm[] = "realmintegrationtest";
const auto kTimeout = zx::sec(5);

// This test exercises the fact that two components should be in separate jobs,
// and thus when one component controller kills its job due to a .Kill() call
// the other component should run uninterrupted.
TEST_F(RealmTest, CreateTwoKillOne) {
  auto enclosing_environment = CreateNewEnclosingEnvironment(kRealm);
  ASSERT_TRUE(WaitForEnclosingEnvToStart(enclosing_environment.get()));
  // Launch two components
  auto controller1 = RunComponent(enclosing_environment.get(), "/boot/bin/sh");

  // launch second component as a service.
  ASSERT_EQ(ZX_OK, enclosing_environment->AddServiceWithLaunchInfo(
                       CreateLaunchInfo("echo2_server_cpp"),
                       fidl::examples::echo::Echo::Name_));

  // make sure echo service is running.
  fidl::examples::echo::EchoPtr echo;
  enclosing_environment->ConnectToService(echo.NewRequest());
  const std::string message = "CreateTwoKillOne";
  fidl::StringPtr ret_msg = "";
  echo->EchoString(message,
                   [&](::fidl::StringPtr retval) { ret_msg = retval; });
  ASSERT_TRUE(RunLoopWithTimeoutOrUntil(
      [&] { return std::string(ret_msg) == message; }, kTimeout));

  // Kill one of the two components, make sure it's exited via Wait
  bool wait = false;
  controller1->Wait([&wait](int64_t errcode) { wait = true; });
  controller1->Kill();
  EXPECT_TRUE(RunLoopWithTimeoutOrUntil([&wait] { return wait; }, kTimeout));

  // Make sure the second component is still running.
  ret_msg = "";
  echo->EchoString(message,
                   [&](::fidl::StringPtr retval) { ret_msg = retval; });
  ASSERT_TRUE(RunLoopWithTimeoutOrUntil(
      [&] { return std::string(ret_msg) == message; }, kTimeout));
}

TEST_F(RealmTest, KillRealmKillsComponent) {
  auto enclosing_environment = CreateNewEnclosingEnvironment(kRealm);
  ASSERT_TRUE(WaitForEnclosingEnvToStart(enclosing_environment.get()));
  ASSERT_EQ(ZX_OK, enclosing_environment->AddServiceWithLaunchInfo(
                       CreateLaunchInfo("echo2_server_cpp"),
                       fidl::examples::echo::Echo::Name_));

  // make sure echo service is running.
  fidl::examples::echo::EchoPtr echo;
  enclosing_environment->ConnectToService(echo.NewRequest());
  const std::string message = "CreateTwoKillOne";
  fidl::StringPtr ret_msg = "";
  echo->EchoString(message,
                   [&](::fidl::StringPtr retval) { ret_msg = retval; });
  ASSERT_TRUE(RunLoopWithTimeoutOrUntil(
      [&] { return std::string(ret_msg) == message; }, kTimeout));

  bool killed = false;
  echo.set_error_handler([&] { killed = true; });
  enclosing_environment->Kill();
  EXPECT_TRUE(RunLoopWithTimeoutOrUntil(
      [&] { return enclosing_environment->is_running(); }, kTimeout));
  // send a msg, without that error handler won't be called.
  echo->EchoString(message,
                   [&](::fidl::StringPtr retval) { ret_msg = retval; });
  EXPECT_TRUE(RunLoopWithTimeoutOrUntil([&] { return killed; }, kTimeout));
}

class RealmFakeLoaderTest : public RealmTest, public fuchsia::sys::Loader {
 protected:
  RealmFakeLoaderTest() {
    loader_service_ =
        fbl::AdoptRef(new fs::Service([this](zx::channel channel) {
          bindings_.AddBinding(
              this,
              fidl::InterfaceRequest<fuchsia::sys::Loader>(std::move(channel)));
          return ZX_OK;
        }));
    enclosing_environment_ =
        CreateNewEnclosingEnvironmentWithLoader(kRealm, loader_service_);
  }

  void LoadComponent(fidl::StringPtr url,
                     LoadComponentCallback callback) override {
    ASSERT_TRUE(component_url_.empty());
    component_url_ = url.get();
  }

  bool WaitForComponentLoad() {
    return RunLoopWithTimeoutOrUntil([this] { return !component_url_.empty(); },
                                     kTimeout);
  }

  const std::string& component_url() const { return component_url_; }

  std::unique_ptr<EnclosingEnvironment> enclosing_environment_;

 private:
  fbl::RefPtr<fs::Service> loader_service_;
  fidl::BindingSet<fuchsia::sys::Loader> bindings_;
  std::string component_url_;
};

TEST_F(RealmFakeLoaderTest, CreateWebComponent_HTTP) {
  RunComponent(enclosing_environment_.get(), "http://example.com");
  ASSERT_TRUE(WaitForComponentLoad());
  EXPECT_EQ("file://web_runner", component_url());
}

TEST_F(RealmFakeLoaderTest, CreateWebComponent_HTTPS) {
  RunComponent(enclosing_environment_.get(), "https://example.com");
  ASSERT_TRUE(WaitForComponentLoad());
  EXPECT_EQ("file://web_runner", component_url());
}

TEST_F(RealmFakeLoaderTest, CreateInvalidComponent) {
  TerminationReason reason = TerminationReason::UNKNOWN;
  int64_t return_code = INT64_MAX;
  auto controller =
      RunComponent(enclosing_environment_.get(), "garbage://test");
  controller.events().OnTerminated = [&](int64_t err, TerminationReason r) {
    return_code = err;
    reason = r;
  };
  ASSERT_TRUE(RunLoopWithTimeoutOrUntil([&] { return return_code < INT64_MAX; },
                                        kTimeout));
  EXPECT_EQ(TerminationReason::URL_INVALID, reason);
  EXPECT_EQ(-1, return_code);
}

}  // namespace
}  // namespace component
