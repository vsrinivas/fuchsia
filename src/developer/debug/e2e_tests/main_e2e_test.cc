// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/e2e_tests/main_e2e_test.h"

#include <lib/syslog/cpp/macros.h>

#include <gtest/gtest.h>

#include "src/developer/debug/e2e_tests/ffx_debug_agent_bridge.h"
#include "src/developer/debug/zxdb/common/err.h"
#include "src/lib/fxl/test/test_settings.h"

zxdb::FfxDebugAgentBridge* bridge = nullptr;

int main(int argc, char* argv[], char* env[]) {
  if (!fxl::SetTestSettings(argc, argv)) {
    return EXIT_FAILURE;
  }

  zxdb::FfxDebugAgentBridge debug_agent_bridge(argv[0], env);

  zxdb::Err e = debug_agent_bridge.Init();
  if (e.has_error()) {
    FX_LOGS(ERROR) << "Failed to initialize debug_agent bridge: " << e.msg();
    return EXIT_FAILURE;
  }

  bridge = &debug_agent_bridge;

  testing::InitGoogleTest(&argc, argv);

  return RUN_ALL_TESTS();
}
