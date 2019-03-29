// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gtest/gtest.h>
#include <src/lib/fxl/arraysize.h>
#include <src/lib/fxl/logging.h>
#include <lib/zx/channel.h>
#include <lib/zx/time.h>
#include <zircon/processargs.h>

#include "jobs.h"
#include "processes.h"
#include "test_helper.h"
#include "test_helper_fixture.h"
#include "util.h"

namespace debugger_utils {
namespace {

const char* const kHelloArgv[] = { kTestHelperPath, "hello" };
const char* const kWaitPeerClosedArgv[] = {
    kTestHelperPath, "wait-peer-closed"
};

void WaitChannelReadable(const zx::channel& channel) {
  zx_signals_t pending;
  ASSERT_EQ(channel.wait_one(ZX_CHANNEL_READABLE, zx::time::infinite(),
                             &pending), ZX_OK);
}

void ReadUint64Packet(const zx::channel& channel, uint64_t expected_value) {
  uint64_t packet;
  uint32_t packet_size;
  ASSERT_EQ(channel.read(0, &packet, sizeof(packet), &packet_size,
                         nullptr, 0, nullptr), ZX_OK);
  EXPECT_EQ(packet_size, sizeof(packet));
  EXPECT_EQ(packet, expected_value);
}

TEST_F(TestWithHelper, GetProcessThreads) {
  zx::job parent_job = GetDefaultJob();
  zx::job child_job;

  constexpr size_t kNumExtraThreads = 4;
  static const char* const argv[] = {
    kTestHelperPath,
    "start-n-threads",
    "4",
    nullptr
  };

  // Don't request additional space for new threads. We want to test there
  // being new threads and there being no space for them.
  constexpr size_t kNoExtraThreads = 0;

  ASSERT_EQ(zx::job::create(parent_job, 0, &child_job), ZX_OK);
  ASSERT_EQ(RunHelperProgram(child_job, argv), ZX_OK);

  // Wait until all the helper's threads are running.
  WaitChannelReadable(channel());
  ReadUint64Packet(channel(), kUint64MagicPacketValue);

  std::vector<zx_koid_t> threads;
  size_t num_available_threads = 0;

  size_t try_count = 1;
  size_t num_initial_threads = 1;
  EXPECT_EQ(TryGetProcessThreadKoidsForTesting(
              process(), try_count, num_initial_threads, kNoExtraThreads,
              &threads, &num_available_threads),
            ZX_OK);

  // We only requested space for one new thread so that's all we get.
  EXPECT_EQ(threads.size(), 1u);
  // The main thread and one for each additional thread;
  size_t expected_num_threads = 1 + kNumExtraThreads;
  EXPECT_EQ(expected_num_threads, num_available_threads);

  // Try a second time, this time requesting space for all threads.
  num_initial_threads = num_available_threads;
  EXPECT_EQ(TryGetProcessThreadKoidsForTesting(
              process(), try_count, num_initial_threads, kNoExtraThreads,
              &threads, &num_available_threads),
            ZX_OK);
  EXPECT_EQ(expected_num_threads, threads.size());
  EXPECT_EQ(expected_num_threads, num_available_threads);

  // Try a third time, this time asking for two iterations.
  // The first iteration won't get them all but the second will.
  try_count = 2;
  num_initial_threads = 1;
  threads.clear();
  EXPECT_EQ(TryGetProcessThreadKoidsForTesting(
              process(), try_count, num_initial_threads, kNoExtraThreads,
              &threads, &num_available_threads),
            ZX_OK);
  EXPECT_EQ(expected_num_threads, threads.size());
  EXPECT_EQ(expected_num_threads, num_available_threads);

  // And again for a fourth time, this time using the main entry point.
  try_count = 2;
  threads.clear();
  EXPECT_EQ(GetProcessThreadKoids(process(), try_count, kNoExtraThreads,
                                  &threads, &num_available_threads),
            ZX_OK);
  EXPECT_EQ(expected_num_threads, threads.size());
  EXPECT_EQ(expected_num_threads, num_available_threads);

  // Test a non-successful return.
  zx::process process2;
  zx_status_t status =
      process().duplicate(ZX_DEFAULT_PROCESS_RIGHTS &~ ZX_RIGHT_ENUMERATE,
                          &process2);
  EXPECT_EQ(status, ZX_OK);
  try_count = 1;
  threads.clear();
  EXPECT_EQ(GetProcessThreadKoids(process2, try_count, kNoExtraThreads,
                                  &threads, &num_available_threads),
            ZX_ERR_ACCESS_DENIED);
  EXPECT_EQ(threads.size(), 0u);
  process2.reset();
}

TEST(Processes, Argv) {
  std::unique_ptr<process::ProcessBuilder> builder;
  Argv argv = BuildArgv(kHelloArgv, arraysize(kHelloArgv));
  ASSERT_EQ(CreateProcessBuilder(GetDefaultJob(), kTestHelperPath, argv,
                                 sys::ServiceDirectory::CreateFromNamespace(),
                                 &builder), ZX_OK);
  builder->CloneAll();
  ASSERT_EQ(builder->Prepare(nullptr), ZX_OK);
  EXPECT_TRUE(builder->data().process.is_valid());
  EXPECT_TRUE(builder->data().root_vmar.is_valid());
  EXPECT_GT(builder->data().stack, 0u);
  EXPECT_GT(builder->data().entry, 0u);
  EXPECT_GT(builder->data().vdso_base, 0u);
  EXPECT_GT(builder->data().base, 0u);

  zx::process process;
  EXPECT_EQ(builder->Start(&process), ZX_OK);
  zx_signals_t observed;
  EXPECT_EQ(process.wait_one(ZX_PROCESS_TERMINATED, zx::time::infinite(),
                             &observed), ZX_OK);
  int rc;
  EXPECT_EQ(GetProcessReturnCode(process.get(), &rc), ZX_OK);
  EXPECT_EQ(rc, 0);
}

// We don't need to test all the ProcessBuilder API, but it's useful to
// test a few things we use.

TEST(Processes, PassHandle) {
  std::unique_ptr<process::ProcessBuilder> builder;
  Argv argv = BuildArgv(kWaitPeerClosedArgv, arraysize(kWaitPeerClosedArgv));
  ASSERT_EQ(CreateProcessBuilder(GetDefaultJob(), kTestHelperPath, argv,
                                 sys::ServiceDirectory::CreateFromNamespace(),
                                 &builder), ZX_OK);
  builder->CloneAll();

  zx::channel our_channel, their_channel;
  ASSERT_EQ(zx::channel::create(0, &our_channel, &their_channel), ZX_OK);
  builder->AddHandle(PA_HND(PA_USER0, 0), std::move(their_channel));

  ASSERT_EQ(builder->Prepare(nullptr), ZX_OK);

  zx::process process;
  EXPECT_EQ(builder->Start(&process), ZX_OK);

  zx_signals_t pending;
  ASSERT_EQ(our_channel.wait_one(ZX_CHANNEL_READABLE, zx::time::infinite(),
                                 &pending), ZX_OK);

  zx::thread thread;
  uint32_t actual_bytes, actual_handles;
  ASSERT_EQ(our_channel.read(0u, nullptr, 0u,
                             &actual_bytes, thread.reset_and_get_address(),
                             1u, &actual_handles), ZX_OK);
  EXPECT_EQ(actual_bytes, 0u);
  EXPECT_EQ(actual_handles, 1u);

  // At this point the inferior is waiting for us to close the channel.
  our_channel.reset();

  EXPECT_EQ(process.wait_one(ZX_PROCESS_TERMINATED, zx::time::infinite(),
                             &pending), ZX_OK);
  int rc;
  EXPECT_EQ(GetProcessReturnCode(process.get(), &rc), ZX_OK);
  EXPECT_EQ(rc, 0);
}

}  // namespace
}  // namespace debugger_utils
