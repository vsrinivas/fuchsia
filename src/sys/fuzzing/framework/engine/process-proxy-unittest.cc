// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/sys/fuzzing/framework/engine/process-proxy.h"

#include <lib/sync/completion.h>

#include <sstream>

#include <gtest/gtest.h>

#include "src/sys/fuzzing/common/options.h"
#include "src/sys/fuzzing/framework/engine/process-proxy-test.h"
#include "src/sys/fuzzing/framework/target/module.h"
#include "src/sys/fuzzing/framework/testing/module.h"
#include "src/sys/fuzzing/framework/testing/target.h"

namespace fuzzing {
namespace {

// Unit tests.

TEST_F(ProcessProxyTest, AddDefaults) {
  Options options;
  ProcessProxyImpl::AddDefaults(&options);
  EXPECT_EQ(options.malloc_exitcode(), kDefaultMallocExitcode);
  EXPECT_EQ(options.death_exitcode(), kDefaultDeathExitcode);
  EXPECT_EQ(options.leak_exitcode(), kDefaultLeakExitcode);
  EXPECT_EQ(options.oom_exitcode(), kDefaultOomExitcode);
}

TEST_F(ProcessProxyTest, Connect) {
  ProcessProxyImpl impl(dispatcher(), pool());

  uint32_t runs = 1000;
  int64_t run_limit = 20;
  auto options1 = ProcessProxyTest::DefaultOptions();
  options1->set_runs(runs);
  options1->set_run_limit(run_limit);
  impl.Configure(options1);
  impl.SetHandlers(IgnoreReceivedSignals, IgnoreErrors);
  auto proxy = Bind(&impl);
  Options options2;
  TestTarget target;
  EXPECT_EQ(proxy->Connect(IgnoreSentSignals(), IgnoreTarget(), &options2), ZX_OK);
  EXPECT_EQ(options2.runs(), runs);
  EXPECT_EQ(options2.run_limit(), run_limit);
}

TEST_F(ProcessProxyTest, AddFeedback) {
  ProcessProxyImpl impl(dispatcher(), pool());

  FakeModule fake;
  fake[0] = 1;
  fake[1] = 4;
  fake[2] = 8;
  Module module(fake.counters(), fake.pcs(), fake.num_pcs());

  Feedback feedback;
  feedback.set_id(module.id());
  feedback.set_inline_8bit_counters(module.Share());

  auto proxy = Bind(&impl);
  proxy->AddFeedback(std::move(feedback));
  auto* module_impl = pool()->Get(module.id(), fake.num_pcs());
  EXPECT_EQ(module_impl->Measure(), 3U);
}

TEST_F(ProcessProxyTest, SignalPeer) {
  ProcessProxyImpl impl(dispatcher(), pool());
  impl.Configure(ProcessProxyTest::DefaultOptions());
  impl.SetHandlers(IgnoreReceivedSignals, IgnoreErrors);
  zx_signals_t observed;
  sync_completion_t sync;
  SignalCoordinator coordinator;
  auto eventpair = coordinator.Create([&](zx_signals_t signals) {
    observed = signals;
    sync_completion_signal(&sync);
    return true;
  });

  auto proxy = Bind(&impl);
  EXPECT_EQ(proxy->Connect(std::move(eventpair), IgnoreTarget(), IgnoreOptions()), ZX_OK);

  impl.Start(/* detect_leaks */ false);
  sync_completion_wait(&sync, ZX_TIME_INFINITE);
  sync_completion_reset(&sync);
  EXPECT_EQ(observed, kStart);

  impl.Start(/* detect_leaks */ true);
  sync_completion_wait(&sync, ZX_TIME_INFINITE);
  sync_completion_reset(&sync);
  EXPECT_EQ(observed, kStartLeakCheck);

  impl.Finish();
  sync_completion_wait(&sync, ZX_TIME_INFINITE);
  sync_completion_reset(&sync);
  EXPECT_EQ(observed, kFinish);
}

TEST_F(ProcessProxyTest, AwaitSignals) {
  ProcessProxyImpl impl(dispatcher(), pool());
  impl.Configure(ProcessProxyTest::DefaultOptions());
  sync_completion_t sync;
  ProcessProxyImpl* failed_impl = nullptr;
  impl.SetHandlers([&]() { sync_completion_signal(&sync); },
                   [&](ProcessProxyImpl* failed) {
                     failed_impl = failed;
                     sync_completion_signal(&sync);
                   });

  auto proxy = Bind(&impl);
  SignalCoordinator coordinator;
  auto eventpair = coordinator.Create([](zx_signals_t signals) { return true; });
  EXPECT_EQ(proxy->Connect(std::move(eventpair), IgnoreTarget(), IgnoreOptions()), ZX_OK);

  sync_completion_reset(&sync);
  coordinator.SignalPeer(kStart);
  sync_completion_wait(&sync, ZX_TIME_INFINITE);

  sync_completion_reset(&sync);
  coordinator.SignalPeer(kFinish);
  sync_completion_wait(&sync, ZX_TIME_INFINITE);
  EXPECT_FALSE(impl.leak_suspected());

  sync_completion_reset(&sync);
  coordinator.SignalPeer(kFinishWithLeaks);
  sync_completion_wait(&sync, ZX_TIME_INFINITE);
  EXPECT_TRUE(impl.leak_suspected());

  sync_completion_reset(&sync);
  coordinator.Reset();
  sync_completion_wait(&sync, ZX_TIME_INFINITE);
  EXPECT_EQ(failed_impl, &impl);
}

TEST_F(ProcessProxyTest, GetStats) {
  ProcessProxyImpl impl(dispatcher(), pool());
  impl.Configure(ProcessProxyTest::DefaultOptions());
  impl.SetHandlers(IgnoreReceivedSignals, IgnoreErrors);
  auto proxy = Bind(&impl);
  TestTarget target;
  zx::process spawned = target.Launch();
  zx_info_handle_basic_t basic_info;
  zx_info_task_stats_t task_stats;
  zx_info_task_runtime_t task_runtime;
  EXPECT_EQ(
      spawned.get_info(ZX_INFO_HANDLE_BASIC, &basic_info, sizeof(basic_info), nullptr, nullptr),
      ZX_OK);
  EXPECT_EQ(spawned.get_info(ZX_INFO_TASK_STATS, &task_stats, sizeof(task_stats), nullptr, nullptr),
            ZX_OK);
  EXPECT_EQ(
      spawned.get_info(ZX_INFO_TASK_RUNTIME, &task_runtime, sizeof(task_runtime), nullptr, nullptr),
      ZX_OK);

  EXPECT_EQ(proxy->Connect(IgnoreSentSignals(), std::move(spawned), IgnoreOptions()), ZX_OK);
  ProcessStats stats;
  impl.GetStats(&stats);
  EXPECT_EQ(stats.koid, basic_info.koid);
  EXPECT_GE(stats.mem_mapped_bytes, task_stats.mem_mapped_bytes);
  EXPECT_GE(stats.mem_private_bytes, task_stats.mem_private_bytes);
  EXPECT_EQ(stats.mem_shared_bytes, task_stats.mem_shared_bytes);
  EXPECT_EQ(stats.mem_scaled_shared_bytes, task_stats.mem_scaled_shared_bytes);
  EXPECT_GE(stats.cpu_time, task_runtime.cpu_time);
  EXPECT_GE(stats.page_fault_time, task_runtime.page_fault_time);
  EXPECT_GE(stats.lock_contention_time, task_runtime.lock_contention_time);
}

TEST_F(ProcessProxyTest, DefaultBadMalloc) {
  ProcessProxyImpl impl(dispatcher(), pool());
  impl.Configure(ProcessProxyTest::DefaultOptions());
  impl.SetHandlers(IgnoreReceivedSignals, IgnoreErrors);
  auto proxy = Bind(&impl);
  TestTarget target;
  EXPECT_EQ(proxy->Connect(IgnoreSentSignals(), target.Launch(), IgnoreOptions()), ZX_OK);
  target.Exit(kDefaultMallocExitcode);
  EXPECT_EQ(impl.Join(), Result::BAD_MALLOC);
}

TEST_F(ProcessProxyTest, CustomBadMalloc) {
  ProcessProxyImpl impl(dispatcher(), pool());
  int32_t exitcode = 1234;
  auto options = ProcessProxyTest::DefaultOptions();
  options->set_malloc_exitcode(exitcode);
  impl.Configure(options);
  impl.SetHandlers(IgnoreReceivedSignals, IgnoreErrors);
  auto proxy = Bind(&impl);
  TestTarget target;
  EXPECT_EQ(proxy->Connect(IgnoreSentSignals(), target.Launch(), IgnoreOptions()), ZX_OK);
  target.Exit(exitcode);
  EXPECT_EQ(impl.Join(), Result::BAD_MALLOC);
}

TEST_F(ProcessProxyTest, DefaultDeath) {
  ProcessProxyImpl impl(dispatcher(), pool());
  impl.Configure(ProcessProxyTest::DefaultOptions());
  impl.SetHandlers(IgnoreReceivedSignals, IgnoreErrors);
  auto proxy = Bind(&impl);
  TestTarget target;
  EXPECT_EQ(proxy->Connect(IgnoreSentSignals(), target.Launch(), IgnoreOptions()), ZX_OK);
  target.Exit(kDefaultDeathExitcode);
  EXPECT_EQ(impl.Join(), Result::DEATH);
}

TEST_F(ProcessProxyTest, CustomDeath) {
  ProcessProxyImpl impl(dispatcher(), pool());
  int32_t exitcode = 4321;
  auto options = ProcessProxyTest::DefaultOptions();
  options->set_death_exitcode(exitcode);
  impl.Configure(options);
  impl.SetHandlers(IgnoreReceivedSignals, IgnoreErrors);
  auto proxy = Bind(&impl);
  TestTarget target;
  EXPECT_EQ(proxy->Connect(IgnoreSentSignals(), target.Launch(), IgnoreOptions()), ZX_OK);
  target.Exit(exitcode);
  EXPECT_EQ(impl.Join(), Result::DEATH);
}

TEST_F(ProcessProxyTest, Exit) {
  ProcessProxyImpl impl(dispatcher(), pool());
  impl.Configure(ProcessProxyTest::DefaultOptions());
  impl.SetHandlers(IgnoreReceivedSignals, IgnoreErrors);
  auto proxy = Bind(&impl);
  TestTarget target;
  EXPECT_EQ(proxy->Connect(IgnoreSentSignals(), target.Launch(), IgnoreOptions()), ZX_OK);
  target.Exit(1);
  EXPECT_EQ(impl.Join(), Result::EXIT);
}

TEST_F(ProcessProxyTest, DefaultLeak) {
  ProcessProxyImpl impl(dispatcher(), pool());
  impl.Configure(ProcessProxyTest::DefaultOptions());
  impl.SetHandlers(IgnoreReceivedSignals, IgnoreErrors);
  auto proxy = Bind(&impl);
  TestTarget target;
  EXPECT_EQ(proxy->Connect(IgnoreSentSignals(), target.Launch(), IgnoreOptions()), ZX_OK);
  target.Exit(kDefaultLeakExitcode);
  EXPECT_EQ(impl.Join(), Result::LEAK);
}

TEST_F(ProcessProxyTest, CustomLeak) {
  ProcessProxyImpl impl(dispatcher(), pool());
  int32_t exitcode = 5678309;
  auto options = ProcessProxyTest::DefaultOptions();
  options->set_leak_exitcode(exitcode);
  impl.Configure(options);
  impl.SetHandlers(IgnoreReceivedSignals, IgnoreErrors);
  auto proxy = Bind(&impl);
  TestTarget target;
  EXPECT_EQ(proxy->Connect(IgnoreSentSignals(), target.Launch(), IgnoreOptions()), ZX_OK);
  target.Exit(exitcode);
  EXPECT_EQ(impl.Join(), Result::LEAK);
}

TEST_F(ProcessProxyTest, DefaultOom) {
  ProcessProxyImpl impl(dispatcher(), pool());
  impl.Configure(ProcessProxyTest::DefaultOptions());
  impl.SetHandlers(IgnoreReceivedSignals, IgnoreErrors);
  auto proxy = Bind(&impl);
  TestTarget target;
  EXPECT_EQ(proxy->Connect(IgnoreSentSignals(), target.Launch(), IgnoreOptions()), ZX_OK);
  target.Exit(kDefaultOomExitcode);
  EXPECT_EQ(impl.Join(), Result::OOM);
}

TEST_F(ProcessProxyTest, CustomOom) {
  ProcessProxyImpl impl(dispatcher(), pool());
  int32_t exitcode = 24601;
  auto options = ProcessProxyTest::DefaultOptions();
  options->set_oom_exitcode(exitcode);
  impl.Configure(options);
  impl.SetHandlers(IgnoreReceivedSignals, IgnoreErrors);
  auto proxy = Bind(&impl);
  TestTarget target;
  EXPECT_EQ(proxy->Connect(IgnoreSentSignals(), target.Launch(), IgnoreOptions()), ZX_OK);
  target.Exit(exitcode);
  EXPECT_EQ(impl.Join(), Result::OOM);
}

TEST_F(ProcessProxyTest, Timeout) {
  ProcessProxyImpl impl(dispatcher(), pool());
  impl.Configure(ProcessProxyTest::DefaultOptions());
  impl.SetHandlers(IgnoreReceivedSignals, IgnoreErrors);
  auto proxy = Bind(&impl);
  TestTarget target;
  EXPECT_EQ(proxy->Connect(IgnoreSentSignals(), target.Launch(), IgnoreOptions()), ZX_OK);
  constexpr size_t kBufSize = 1U << 20;
  auto buf = std::make_unique<char[]>(kBufSize);
  // On timeout, the runner invokes |ProcessProxyImpl::Dump|.
  auto len = impl.Dump(buf.get(), kBufSize);
  EXPECT_GT(len, 0U);
  EXPECT_LT(len, kBufSize);
  impl.Kill();
}

}  // namespace
}  // namespace fuzzing
