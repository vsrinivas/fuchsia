// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/modular/testing/cpp/fidl.h>
#include <lib/modular/testing/cpp/fake_component.h>

#include <iostream>

#include "src/lib/files/glob.h"
#include "src/modular/lib/modular_test_harness/cpp/test_harness_fixture.h"
constexpr char kModularTestHarnessGlobPath[] =
    "/hub/r/mth_*_test/*/c/sessionmgr.cmx/*/out/debug/sessionctl";

class SessionctlTest : public modular_testing::TestHarnessFixture {};

TEST_F(SessionctlTest, FindSessionCtlService) {
  fuchsia::modular::testing::TestHarnessSpec spec;
  spec.set_environment_suffix("test");
  modular_testing::TestHarnessBuilder builder(std::move(spec));
  builder.BuildAndRun(test_harness());

  RunLoopUntil([&] { return files::Glob(kModularTestHarnessGlobPath).size() == 1; });
}
