// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/modular/cpp/fidl.h>
#include <fuchsia/modular/internal/cpp/fidl.h>
#include <fuchsia/sys/cpp/fidl.h>
#include <lib/async/default.h>
#include <lib/sys/cpp/testing/test_with_environment.h>
#include <src/lib/fxl/strings/join_strings.h>

#include "src/lib/files/directory.h"
#include "src/lib/files/file.h"
#include "src/lib/files/glob.h"

#include "gtest/gtest.h"
#include "peridot/bin/sessionctl/session_ctl_constants.h"

namespace sessionctl {

class SessionCtlTest : public sys::testing::TestWithEnvironment {
 protected:
  void RunBasemgr() {
    std::vector<std::string> args;
    args.push_back("--test");
    args.push_back("--run_base_shell_with_test_runner=false");
    args.push_back(
        "--base_shell=fuchsia-pkg://fuchsia.com/dev_base_shell#meta/"
        "dev_base_shell.cmx");
    args.push_back(
        "--session_shell=fuchsia-pkg://fuchsia.com/dev_session_shell#meta/"
        "dev_session_shell.cmx");
    args.push_back(
        "--story_shell=fuchsia-pkg://fuchsia.com/dev_story_shell#meta/"
        "dev_story_shell.cmx");
    args.push_back("--sessionmgr_args=--use_memfs_for_ledger");

    RunComponent("fuchsia-pkg://fuchsia.com/basemgr#meta/basemgr.cmx", args);
  }

  void RunComponent(const std::string& component_url,
                    const std::vector<std::string>& args) {
    fuchsia::sys::LaunchInfo launch_info;
    launch_info.url = component_url;
    for (auto arg : args) {
      launch_info.arguments.push_back(arg);
    }

    fuchsia::sys::ComponentControllerPtr controller;
    launcher_ptr()->CreateComponent(std::move(launch_info),
                                    controller.NewRequest());
    component_ptrs_.push_back(std::move(controller));

    RunLoopWithTimeout(zx::sec(10));
  }

  std::vector<fuchsia::sys::ComponentControllerPtr> component_ptrs_;
};

TEST_F(SessionCtlTest, FindSessionCtlService) {
  RunBasemgr();

  files::Glob sessionctl_service(modular::kSessionCtlServiceGlobPath);
  EXPECT_EQ(sessionctl_service.size(), 1u)
      << modular::kSessionCtlServiceGlobPath << " expected to match once.";
}

}  // namespace sessionctl
