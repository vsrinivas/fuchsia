// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/appmgr/component_controller_impl.h"
#include "garnet/bin/appmgr/realm.h"

#include <stdint.h>
#include <stdio.h>
#include <unistd.h>
#include <fstream>
#include <string>
#include <vector>

#include <fidl/examples/echo/cpp/fidl.h>
#include <fs/pseudo-dir.h>
#include <fs/synchronous-vfs.h>
#include <fuchsia/sys/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async/default.h>
#include <lib/fdio/util.h>
#include "garnet/bin/appmgr/util.h"
#include "gtest/gtest.h"
#include "lib/component/cpp/testing/test_util.h"
#include "lib/component/cpp/testing/test_with_environment.h"
#include "lib/fidl/cpp/binding_set.h"
#include "lib/fxl/files/scoped_temp_dir.h"
#include "lib/fxl/logging.h"

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
    OpenNewOutFile();
  }

  void OpenNewOutFile() {
    ASSERT_TRUE(tmp_dir_.NewTempFile(&out_file_));
    outf_ = fileno(std::fopen(out_file_.c_str(), "w"));
  }

  std::string ReadOutFile() {
    std::string out;
    if (!files::ReadFileToString(out_file_, &out)) {
      FXL_LOG(ERROR) << "Could not read output file " << out_file_;
      return "";
    }
    return out;
  }

  fuchsia::sys::LaunchInfo CreateLaunchInfo(
      const std::string& url, const std::vector<std::string>& args = {}) {
    fuchsia::sys::LaunchInfo launch_info;
    launch_info.url = url;
    for (const auto& a : args) {
      launch_info.arguments.push_back(a);
    }
    launch_info.out = CloneFileDescriptor(outf_);
    launch_info.err = CloneFileDescriptor(STDERR_FILENO);
    return launch_info;
  }

  fuchsia::sys::ComponentControllerPtr RunComponent(
      EnclosingEnvironment* enclosing_environment, const std::string& url,
      const std::vector<std::string>& args = {}) {
    return enclosing_environment->CreateComponent(
        CreateLaunchInfo(url, std::move(args)));
  }

 private:
  files::ScopedTempDir tmp_dir_;
  std::string out_file_;
  int outf_;
};

constexpr char kRealm[] = "realmintegrationtest";
const auto kTimeout = zx::sec(5);

TEST_F(RealmTest, Resolve) {
  auto enclosing_environment =
      CreateNewEnclosingEnvironment(kRealm, CreateServices());

  fidl::InterfacePtr<fuchsia::process::Resolver> resolver;
  enclosing_environment->ConnectToService(resolver.NewRequest());

  bool wait = false;
  resolver->Resolve(
      fidl::StringPtr("fuchsia-pkg://fuchsia.com/appmgr_integration_tests#test/"
                      "appmgr_realm_integration_tests"),
      [&wait](zx_status_t status, zx::vmo binary,
              fidl::InterfaceHandle<fuchsia::ldsvc::Loader> loader) {
        wait = true;

        ASSERT_EQ(ZX_OK, status);

        std::string expect;
        // One day, when this test is not run in the shell realm, it should
        // read:
        // files::ReadFileToString("/pkg/test/appmgr_realm_integration_tests",
        // &expect);
        files::ReadFileToString(
            "/pkgfs/packages/appmgr_integration_tests/0/test/"
            "appmgr_realm_integration_tests",
            &expect);
        ASSERT_FALSE(expect.empty());

        std::vector<char> buf(expect.length());
        ASSERT_EQ(ZX_OK, binary.read(buf.data(), 0, buf.size()));
        std::string actual(buf.begin(), buf.end());

        ASSERT_EQ(expect, actual);
      });
  EXPECT_TRUE(RunLoopWithTimeoutOrUntil([&wait] { return wait; }, kTimeout));
}

TEST_F(RealmTest, LaunchNonExistentComponent) {
  auto env_services = CreateServices();
  auto enclosing_environment =
      CreateNewEnclosingEnvironment(kRealm, std::move(env_services));
  ASSERT_TRUE(WaitForEnclosingEnvToStart(enclosing_environment.get()));

  // try to launch file url.
  auto controller1 =
      RunComponent(enclosing_environment.get(), "does_not_exist");
  bool wait = false;
  controller1.events().OnTerminated =
      [&wait](int64_t return_code, fuchsia::sys::TerminationReason reason) {
        wait = true;
        EXPECT_EQ(reason, fuchsia::sys::TerminationReason::PACKAGE_NOT_FOUND);
      };
  EXPECT_TRUE(RunLoopWithTimeoutOrUntil([&wait] { return wait; }, kTimeout));

  // try to launch pkg url.
  auto controller2 =
      RunComponent(enclosing_environment.get(),
                   "fuchsia-pkg://fuchsia.com/does_not_exist#meta/some.cmx");
  wait = false;
  controller2.events().OnTerminated =
      [&wait](int64_t return_code, fuchsia::sys::TerminationReason reason) {
        wait = true;
        EXPECT_EQ(reason, fuchsia::sys::TerminationReason::PACKAGE_NOT_FOUND);
      };
  EXPECT_TRUE(RunLoopWithTimeoutOrUntil([&wait] { return wait; }, kTimeout));
}

// This test exercises the fact that two components should be in separate jobs,
// and thus when one component controller kills its job due to a .Kill() call
// the other component should run uninterrupted.
TEST_F(RealmTest, CreateTwoKillOne) {
  // launch component as a service.
  auto env_services = CreateServices();
  ASSERT_EQ(ZX_OK, env_services->AddServiceWithLaunchInfo(
                       CreateLaunchInfo("echo2_server_cpp"),
                       fidl::examples::echo::Echo::Name_));
  auto enclosing_environment =
      CreateNewEnclosingEnvironment(kRealm, std::move(env_services));
  ASSERT_TRUE(WaitForEnclosingEnvToStart(enclosing_environment.get()));
  // launch component normally
  auto controller1 =
      RunComponent(enclosing_environment.get(), "echo2_server_cpp");

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
  controller1.events().OnTerminated =
      [&wait](int64_t return_code, fuchsia::sys::TerminationReason reason) {
        wait = true;
      };
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
  auto env_services = CreateServices();
  ASSERT_EQ(ZX_OK, env_services->AddServiceWithLaunchInfo(
                       CreateLaunchInfo("echo2_server_cpp"),
                       fidl::examples::echo::Echo::Name_));
  auto enclosing_environment =
      CreateNewEnclosingEnvironment(kRealm, std::move(env_services));
  ASSERT_TRUE(WaitForEnclosingEnvToStart(enclosing_environment.get()));

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
  echo.set_error_handler([&](zx_status_t status) { killed = true; });
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
    enclosing_environment_ = CreateNewEnclosingEnvironment(
        kRealm, CreateServicesWithCustomLoader(loader_service_));
  }

  void LoadUrl(fidl::StringPtr url, LoadUrlCallback callback) override {
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
