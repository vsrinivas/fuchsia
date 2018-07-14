// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/appmgr/component_controller_impl.h"
#include "garnet/bin/appmgr/realm.h"

#include <stdint.h>
#include <stdio.h>
#include <unistd.h>

#include <fs/pseudo-dir.h>
#include <fs/synchronous-vfs.h>
#include <fuchsia/sys/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async/default.h>
#include <lib/fdio/util.h>

#include "garnet/bin/appmgr/util.h"
#include "gtest/gtest.h"
#include "lib/component/cpp/environment_services.h"
#include "lib/fidl/cpp/binding_set.h"
#include "lib/gtest/real_loop_fixture.h"

namespace component {
namespace {

using fuchsia::sys::TerminationReason;

// TODO(geb): Much of this code can be simplified once TestWithEnvironment is
// checked in.

fuchsia::sys::FileDescriptorPtr CloneFileDescriptor(int fd) {
  zx_handle_t handles[FDIO_MAX_HANDLES] = {0, 0, 0};
  uint32_t types[FDIO_MAX_HANDLES] = {
      ZX_HANDLE_INVALID,
      ZX_HANDLE_INVALID,
      ZX_HANDLE_INVALID,
  };
  zx_status_t status = fdio_clone_fd(fd, 0, handles, types);
  if (status <= 0)
    return nullptr;
  fuchsia::sys::FileDescriptorPtr result = fuchsia::sys::FileDescriptor::New();
  result->type0 = types[0];
  result->handle0 = zx::handle(handles[0]);
  result->type1 = types[1];
  result->handle1 = zx::handle(handles[1]);
  result->type2 = types[2];
  result->handle2 = zx::handle(handles[2]);
  return result;
}

fuchsia::sys::ComponentControllerPtr RunComponent(
    fuchsia::sys::LauncherPtr& launcher, std::string component_url) {
  std::FILE* tmpf_= std::tmpfile();
  const int tmp_fd = fileno(tmpf_);

  fuchsia::sys::LaunchInfo launch_info;
  launch_info.url = component_url;
  launch_info.out = CloneFileDescriptor(tmp_fd);

  fuchsia::sys::ComponentControllerPtr controller;
  launcher->CreateComponent(std::move(launch_info), controller.NewRequest());

  close(tmp_fd);

  return controller;
}

class RealmTest : public ::gtest::RealLoopFixture {};

// This test exercises the fact that two components should be in separate jobs,
// and thus when one component controller kills its job due to a .Kill() call
// the other component should run uninterrupted.
TEST_F(RealmTest, CreateTwoKillOne) {
  // Connect to the Launcher service through our static environment.
  // This launcher is from sys realm so our hub would be scoped to it
  fuchsia::sys::LauncherPtr launcher;
  component::ConnectToEnvironmentService(launcher.NewRequest());

  // Launch two components
  fuchsia::sys::ComponentControllerPtr controller1 =
      RunComponent(launcher, "/boot/bin/sh");

  fuchsia::sys::ComponentControllerPtr controller2 =
      RunComponent(launcher, "/boot/bin/sh");

  bool controller2_had_error = false;
  controller2.set_error_handler(
      [&controller2_had_error] { controller2_had_error = true; });

  // Kill one of the two components, make sure it's exited via Wait

  bool wait = false;
  controller1->Wait([&wait](int64_t errcode) { wait = true; });
  controller1->Kill();
  EXPECT_TRUE(RunLoopWithTimeoutOrUntil([&wait] { return wait; }, zx::sec(5)));

  // Make sure the second controller didn't have any errors
  EXPECT_FALSE(RunLoopWithTimeoutOrUntil(
      [&controller2_had_error] { return controller2_had_error; }, zx::sec(2)));

  // Kill the other component
  controller2->Kill();

  RunLoopUntilIdle();
}

const char kRealm[] = "realmintegrationtest";

class RealmFakeLoaderTest : public RealmTest,
                            public fuchsia::sys::Loader {
 protected:
  RealmFakeLoaderTest()
      : vfs_(async_get_default_dispatcher()),
        services_(fbl::AdoptRef(new fs::PseudoDir)) {}

  void SetUp() override {
    fuchsia::sys::EnvironmentPtr sys_env;
    component::ConnectToEnvironmentService(sys_env.NewRequest());
    // Fake loader service that checks url.
    zx_status_t status = services_->AddEntry(
        fuchsia::sys::Loader::Name_,
        fbl::AdoptRef(new fs::Service([this](zx::channel channel) {
          bindings_.AddBinding(
              this,
              fidl::InterfaceRequest<fuchsia::sys::Loader>(
                  std::move(channel)));
          return ZX_OK;
        })));
    ASSERT_EQ(ZX_OK, status);

    // Start nested environment.
    sys_env->CreateNestedEnvironment(
        component::Util::OpenAsDirectory(&vfs_, services_),
        nested_env_.NewRequest(), controller_.NewRequest(), kRealm);
    nested_env_->GetLauncher(launcher_.NewRequest());
  }

  void LoadComponent(fidl::StringPtr url, LoadComponentCallback callback)
      override {
    ASSERT_TRUE(component_url_.empty());
    component_url_ = url.get();
  }

  bool WaitForComponentLoad() {
    return RunLoopWithTimeoutOrUntil(
        [this] { return !component_url_.empty(); }, zx::sec(10));
  }

  void TearDown() override {
    if (controller_) {
      bool alive = true;
      controller_.set_error_handler([&alive] { alive = false; });
      controller_->Kill([](){});
      ASSERT_TRUE(RunLoopWithTimeoutOrUntil(
          [&alive] { return !alive; }, zx::sec(10)));
      controller_.Unbind();
    }
  }

  const std::string& component_url() const { return component_url_; }

  fuchsia::sys::LauncherPtr launcher_;

 private:
  fs::SynchronousVfs vfs_;
  fbl::RefPtr<fs::PseudoDir> services_;
  fuchsia::sys::EnvironmentPtr nested_env_;
  fuchsia::sys::EnvironmentControllerPtr controller_;
  fidl::BindingSet<fuchsia::sys::Loader> bindings_;
  std::string component_url_;
};

TEST_F(RealmFakeLoaderTest, CreateWebComponent_HTTP) {
  RunComponent(launcher_, "http://example.com");
  ASSERT_TRUE(WaitForComponentLoad());
  EXPECT_EQ("file://web_runner", component_url());
}

TEST_F(RealmFakeLoaderTest, CreateWebComponent_HTTPS) {
  RunComponent(launcher_, "https://example.com");
  ASSERT_TRUE(WaitForComponentLoad());
  EXPECT_EQ("file://web_runner", component_url());
}

TEST_F(RealmFakeLoaderTest, CreateInvalidComponent) {
  TerminationReason reason = TerminationReason::UNKNOWN;
  int64_t return_code = INT64_MAX;
  auto controller = RunComponent(launcher_, "garbage://test");
  controller.events().OnTerminated = [&](int64_t err,
                                         TerminationReason r) {
    return_code = err;
    reason = r;
  };
  ASSERT_TRUE(RunLoopWithTimeoutOrUntil(
      [&] { return return_code < INT64_MAX; }, zx::sec(5)));
  EXPECT_EQ(TerminationReason::URL_INVALID, reason);
  EXPECT_EQ(-1, return_code);
}

}  // namespace
}  // namespace component
