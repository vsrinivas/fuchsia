// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <glob.h>

#include <fuchsia/sys/cpp/fidl.h>
#include <lib/async/default.h>

#include "garnet/bin/appmgr/service_provider_dir_impl.h"
#include "garnet/bin/appmgr/util.h"
#include "lib/component/cpp/environment_services.h"
#include "lib/component/cpp/testing/test_util.h"
#include "lib/component/cpp/testing/test_with_environment.h"
#include "lib/fxl/files/file.h"
#include "lib/fxl/strings/join_strings.h"
#include "lib/fxl/strings/string_printf.h"
#include "lib/svc/cpp/services.h"

namespace component {
namespace {

// This test fixture will provide a way to run components in provided launchers
// and check for errors.
class HubTest : public component::testing::TestWithEnvironment {
 protected:
  // This would launch component and check that it returns correct
  // |expected_return_code|.
  void RunComponent(const fuchsia::sys::LauncherPtr& launcher,
                    const std::string& component_url,
                    const std::vector<std::string>& args,
                    int64_t expected_return_code) {
    std::FILE* outf = std::tmpfile();
    int out_fd = fileno(outf);
    fuchsia::sys::LaunchInfo launch_info;
    launch_info.url = component_url;
    for (auto arg : args) {
      launch_info.arguments.push_back(arg);
    }

    launch_info.out = component::testing::CloneFileDescriptor(out_fd);

    fuchsia::sys::ComponentControllerPtr controller;
    launcher->CreateComponent(std::move(launch_info), controller.NewRequest());

    int64_t return_code = INT64_MIN;
    controller->Wait([&return_code](int64_t code) { return_code = code; });
    ASSERT_TRUE(RunLoopWithTimeoutOrUntil(
        [&return_code] { return return_code != INT64_MIN; }, zx::sec(10)));
    std::string output;
    ASSERT_TRUE(files::ReadFileDescriptorToString(out_fd, &output));
    EXPECT_EQ(expected_return_code, return_code)
        << "failed for: " << fxl::JoinStrings(args, ", ")
        << "\noutput: " << output;
  }
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

TEST_F(HubTest, ScopePolicy) {
  std::string glob_url = "glob";
  // test that we can find logger
  RunComponent(launcher_ptr(), glob_url, {"/hub/c/logger"}, 0);

  // test that we cannot find /hub/r/sys as we are scopped into /hub/r/sys.
  RunComponent(launcher_ptr(), glob_url, {"/hub/r/sys"}, 1);

  // create nested environment
  // test that we can see nested env
  auto nested_env = CreateNewEnclosingEnvironment("hubscopepolicytest");
  ASSERT_TRUE(WaitForEnclosingEnvToStart(nested_env.get()));
  RunComponent(launcher_ptr(), glob_url, {"/hub/r/hubscopepolicytest/"}, 0);

  // test that we cannot see nested env using its own launcher
  RunComponent(nested_env->launcher_ptr(), glob_url,
               {"/hub/r/hubscopepolicytest"}, 1);

  // test that we can see check_hub_path
  RunComponent(nested_env->launcher_ptr(), glob_url, {"/hub/c/glob"}, 0);
}

}  // namespace
}  // namespace component
