// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <glob.h>

#include <fuchsia/sys/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async/default.h>
#include <lib/fdio/limits.h>
#include <lib/fdio/util.h>

#include "garnet/bin/appmgr/service_provider_dir_impl.h"
#include "garnet/bin/appmgr/util.h"
#include "gtest/gtest.h"
#include "lib/app/cpp/environment_services.h"
#include "lib/fxl/strings/join_strings.h"
#include "lib/fxl/strings/string_printf.h"
#include "lib/svc/cpp/services.h"

namespace component {
namespace {

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

// This test fixture will provide a way to create nested environment. On setup
// it will setup the the services required to create a nested environment and
// then provides a API to environment.
class HubTest : public ::testing::Test {
 protected:
  HubTest()
      : loop_(&kAsyncLoopConfigMakeDefault),
        vfs_(async_get_default_dispatcher()),
        services_(fbl::AdoptRef(new ServiceProviderDirImpl())) {
    // we are currently have access to sys environment and not root environment.
    fuchsia::sys::ConnectToEnvironmentService(sys_env_.NewRequest());
    sys_env_->GetServices(svc_.NewRequest());
    services_->AddService(
        fbl::AdoptRef(new fs::Service([this](zx::channel channel) {
          svc_->ConnectToService(fuchsia::sys::Loader::Name_,
                                 std::move(channel));
          return ZX_OK;
        })),
        fuchsia::sys::Loader::Name_);
    loop_.StartThread();
  }

  void CreateNestedEnvironment(
      std::string label, fuchsia::sys::EnvironmentSync2Ptr* nested_env_out) {
    fuchsia::sys::EnvironmentControllerSync2Ptr controller;
    sys_env_->CreateNestedEnvironment(Util::OpenAsDirectory(&vfs_, services_),
                                      nested_env_out->NewRequest(),
                                      controller.NewRequest(), label);

    env_controllers_.push_back(std::move(controller));
  }

 private:
  async::Loop loop_;
  fs::SynchronousVfs vfs_;
  fbl::RefPtr<ServiceProviderDirImpl> services_;
  fuchsia::sys::EnvironmentSync2Ptr sys_env_;
  fuchsia::sys::ServiceProviderSync2Ptr svc_;
  std::vector<fuchsia::sys::EnvironmentControllerSync2Ptr> env_controllers_;
};

TEST(ProbeHub, Component) {
  auto glob_str = fxl::StringPrintf("/hub/c/sysmgr/*/out/debug");
  glob_t globbuf;
  ASSERT_EQ(glob(glob_str.data(), 0, NULL, &globbuf), 0)
      << glob_str << " does not exist.";
  EXPECT_EQ(globbuf.gl_pathc, 1u);
  globfree(&globbuf);
}

TEST(ProbeHub, Realm) {
  auto glob_str = fxl::StringPrintf("/hub/r/sys/*/c/");
  glob_t globbuf;
  ASSERT_EQ(glob(glob_str.data(), 0, NULL, &globbuf), 0)
      << glob_str << " does not exist.";
  EXPECT_EQ(globbuf.gl_pathc, 1u);
  globfree(&globbuf);
}

TEST(ProbeHub, RealmSvc) {
  auto glob_str = fxl::StringPrintf("/hub/r/sys/*/svc/fuchsia.sys.Environment");
  glob_t globbuf;
  ASSERT_EQ(glob(glob_str.data(), 0, NULL, &globbuf), 0)
      << glob_str << " does not exist.";
  EXPECT_EQ(globbuf.gl_pathc, 1u);
  globfree(&globbuf);
}

// This would launch component and check that it returns correct
// |expected_return_code|.
void RunComponent(fuchsia::sys::LauncherSync2Ptr& launcher,
                  std::string component_url, std::vector<std::string> args,
                  int64_t expected_return_code) {
  std::FILE* tmpf = std::tmpfile();
  int tmp_fd = fileno(tmpf);
  fuchsia::sys::LaunchInfo launch_info;
  launch_info.url = component_url;
  for (auto arg : args) {
    launch_info.arguments.push_back(arg);
  }

  launch_info.out = CloneFileDescriptor(tmp_fd);
  launch_info.err = CloneFileDescriptor(STDERR_FILENO);

  fuchsia::sys::ComponentControllerSync2Ptr controller;
  launcher->CreateComponent(std::move(launch_info), controller.NewRequest());

  int64_t return_code;
  controller->Wait(&return_code);
  std::string output;
  int nread;
  char buf[1024];
  while ((nread = read(tmp_fd, buf, sizeof(buf))) > 0) {
    output += std::string(buf, nread);
  }
  ASSERT_NE(-1, nread) << strerror(errno);
  EXPECT_EQ(expected_return_code, return_code)
      << "failed for: " << fxl::JoinStrings(args, ", ")
      << "\noutput: " << output;
}

TEST_F(HubTest, ScopePolicy) {
  // Connect to the Launcher service through our static environment.
  // This launcher is from sys realm so our hub would be scoped to it
  fuchsia::sys::LauncherSync2Ptr launcher;
  fuchsia::sys::ConnectToEnvironmentService(launcher.NewRequest());

  std::string glob_url = "glob";
  // test that we can find logger
  RunComponent(launcher, glob_url, {"/hub/c/logger"}, 0);

  // test that we cannot find /hub/r/sys as we are scopped into /hub/r/sys.
  RunComponent(launcher, glob_url, {"/hub/r/sys"}, 1);

  // create nested environment
  // test that we can see nested env
  fuchsia::sys::EnvironmentSync2Ptr nested_env;
  CreateNestedEnvironment("hubscopepolicytest", &nested_env);
  RunComponent(launcher, glob_url, {"/hub/r/hubscopepolicytest/"}, 0);

  // test that we cannot see nested env using its own launcher
  fuchsia::sys::LauncherSync2Ptr nested_launcher;
  nested_env->GetLauncher(nested_launcher.NewRequest());
  RunComponent(nested_launcher, glob_url, {"/hub/r/hubscopepolicytest"}, 1);

  // test that we can see check_hub_path
  RunComponent(nested_launcher, glob_url, {"/hub/c/glob"}, 0);
}

}  // namespace
}  // namespace component
