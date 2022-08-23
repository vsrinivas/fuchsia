// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_E2E_TESTS_MAIN_E2E_TEST_H_
#define SRC_DEVELOPER_DEBUG_E2E_TESTS_MAIN_E2E_TEST_H_

#include "src/developer/debug/e2e_tests/ffx_debug_agent_bridge.h"

// Pointer to the |FfxDebugAgentBridge| instance which controls the sub-process ffx call and is
// where the UNIX socket path is read. This handle lets us get that path to the |E2eTest| test
// fixture to connect to debug_agent.
extern zxdb::FfxDebugAgentBridge* bridge;

#endif  // SRC_DEVELOPER_DEBUG_E2E_TESTS_MAIN_E2E_TEST_H_
