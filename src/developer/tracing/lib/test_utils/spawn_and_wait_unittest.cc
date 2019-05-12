// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/tracing/lib/test_utils/spawn_and_wait.h"

#include <gtest/gtest.h>
#include <lib/zx/eventpair.h>

namespace tracing {
namespace {

const char kReturnCodeChildPath[] = "/pkg/bin/return_1234";

constexpr int kChildReturnCode = 1234;

const char kSignalPeerChildPath[] = "/pkg/bin/signal_peer";

constexpr int kSignalPeerReturnCode = 4321;

TEST(TraceTestUtils, SpawnAndWait) {
  zx::job job{};  // -> default job
  zx::process child;
  std::vector<std::string> argv{kReturnCodeChildPath};

  ASSERT_EQ(SpawnProgram(job, argv, ZX_HANDLE_INVALID, &child), ZX_OK);

  int return_code;
  ASSERT_EQ(WaitAndGetExitCode(argv[0], child, &return_code), ZX_OK);
  EXPECT_EQ(return_code, kChildReturnCode);
}

TEST(TraceTestUtils, SpawnAndWaitSignalPeer) {
  zx::job job{};  // -> default job
  zx::process child;
  std::vector<std::string> argv{kSignalPeerChildPath};

  zx::eventpair our_event, their_event;
  ASSERT_EQ(zx::eventpair::create(0u, &our_event, &their_event), ZX_OK);

  ASSERT_EQ(SpawnProgram(job, argv, their_event.release(), &child), ZX_OK);

  zx_signals_t pending;
  EXPECT_EQ(our_event.wait_one(ZX_EVENTPAIR_SIGNALED, zx::time::infinite(),
                               &pending),
            ZX_OK);

  int return_code;
  ASSERT_EQ(WaitAndGetExitCode(argv[0], child, &return_code), ZX_OK);
  EXPECT_EQ(return_code, kSignalPeerReturnCode);
}

}  // namespace
}  // namespace tracing
