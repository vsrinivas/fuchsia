// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/modular/testing/cpp/fidl.h>
#include <lib/modular_test_harness/cpp/test_harness_fixture.h>

#include "src/lib/files/glob.h"

constexpr char kModularTestHarnessGlobPath[] =
    "/hub/r/mth_*_test/*/c/sessionmgr.cmx/*/out/debug/sessionctl";

class SessionctlTest : public modular::testing::TestHarnessFixture {};

TEST_F(SessionctlTest, FindSessionCtlService) {
  fuchsia::modular::testing::TestHarnessSpec spec;
  spec.set_environment_suffix("test");
  const auto story_shell_url = InterceptStoryShell(&spec);
  bool found_sessionctl_service = false;

  test_harness().events().OnNewComponent =
      [&](fuchsia::sys::StartupInfo startup_info,
          fidl::InterfaceHandle<fuchsia::modular::testing::InterceptedComponent>
              component) {
        ASSERT_EQ(story_shell_url, startup_info.launch_info.url);
        files::Glob sessionctl_service_glob(kModularTestHarnessGlobPath);
        found_sessionctl_service = sessionctl_service_glob.size() == 1u;
      };
  test_harness()->Run(std::move(spec));

  RunLoopUntil([&] { return found_sessionctl_service; });
}
