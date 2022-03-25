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
  ProcessProxy::AddDefaults(&options);
  EXPECT_EQ(options.malloc_exitcode(), kDefaultMallocExitcode);
  EXPECT_EQ(options.death_exitcode(), kDefaultDeathExitcode);
  EXPECT_EQ(options.leak_exitcode(), kDefaultLeakExitcode);
  EXPECT_EQ(options.oom_exitcode(), kDefaultOomExitcode);
}

TEST_F(ProcessProxyTest, Connect) {
  auto process_proxy = MakeProcessProxy();

  uint32_t runs = 1000;
  int64_t run_limit = 20;
  auto options1 = ProcessProxyTest::DefaultOptions();
  options1->set_runs(runs);
  options1->set_run_limit(run_limit);
  process_proxy->Configure(options1);
  TestTarget target(executor());
  process_proxy->Connect(IgnoreAll());
}

TEST_F(ProcessProxyTest, AddFeedback) {
  auto process_proxy = MakeProcessProxy();

  FakeModule fake;
  fake[0] = 1;
  fake[1] = 4;
  fake[2] = 8;
  Module module(fake.counters(), fake.pcs(), fake.num_pcs());

  auto llvm_module = module.GetLlvmModule();
  process_proxy->AddLlvmModule(std::move(llvm_module));
  auto* module_impl = pool()->Get(module.id(), fake.num_pcs());
  EXPECT_EQ(module_impl->Measure(), 3U);
}

TEST_F(ProcessProxyTest, Signals) {
  auto process_proxy = MakeProcessProxy();
  process_proxy->Configure(ProcessProxyTest::DefaultOptions());

  AsyncEventPair eventpair(executor());
  process_proxy->Connect(IgnoreTarget(eventpair.Create()));
  FUZZING_EXPECT_OK(eventpair.WaitFor(kSync));
  RunUntilIdle();

  EXPECT_EQ(eventpair.SignalSelf(kSync, 0), ZX_OK);
  FUZZING_EXPECT_OK(process_proxy->Start(/* detect_leaks= */ false));
  FUZZING_EXPECT_OK(eventpair.WaitFor(kStart).and_then([&](const zx_signals_t& signals) {
    EXPECT_EQ(eventpair.SignalPeer(0, kStart), ZX_OK);
    return fpromise::ok();
  }));
  RunUntilIdle();

  EXPECT_EQ(eventpair.SignalSelf(kStart, 0), ZX_OK);
  FUZZING_EXPECT_OK(process_proxy->AwaitFinish(), /* leaks_suspected= */ true);
  process_proxy->Finish();
  FUZZING_EXPECT_OK(eventpair.WaitFor(kFinish).and_then([&](const zx_signals_t& signals) {
    EXPECT_EQ(eventpair.SignalPeer(0, kFinishWithLeaks), ZX_OK);
    return fpromise::ok();
  }));
  RunUntilIdle();

  EXPECT_EQ(eventpair.SignalSelf(kFinish, 0), ZX_OK);
  FUZZING_EXPECT_OK(process_proxy->Start(/* detect_leaks= */ true));
  FUZZING_EXPECT_OK(eventpair.WaitFor(kStartLeakCheck).and_then([&](const zx_signals_t& signals) {
    EXPECT_EQ(eventpair.SignalPeer(0, kStart), ZX_OK);
    return fpromise::ok();
  }));
  RunUntilIdle();

  EXPECT_EQ(eventpair.SignalSelf(kStartLeakCheck, 0), ZX_OK);
  FUZZING_EXPECT_OK(process_proxy->AwaitFinish(), /* leaks_suspected= */ false);
  process_proxy->Finish();
  FUZZING_EXPECT_OK(eventpair.WaitFor(kFinish).and_then([&](const zx_signals_t& signals) {
    EXPECT_EQ(eventpair.SignalPeer(0, kFinish), ZX_OK);
    return fpromise::ok();
  }));
  RunUntilIdle();
}

TEST_F(ProcessProxyTest, GetStats) {
  auto process_proxy = MakeProcessProxy();
  process_proxy->Configure(ProcessProxyTest::DefaultOptions());
  TestTarget target(executor());
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

  process_proxy->Connect(IgnoreSentSignals(std::move(spawned)));
  ProcessStats stats;
  process_proxy->GetStats(&stats);
  EXPECT_EQ(stats.koid, basic_info.koid);
  EXPECT_GE(stats.mem_mapped_bytes, task_stats.mem_mapped_bytes);
  EXPECT_NE(stats.mem_private_bytes, 0U);
  EXPECT_GE(stats.cpu_time, task_runtime.cpu_time);
  EXPECT_GE(stats.page_fault_time, task_runtime.page_fault_time);
  EXPECT_GE(stats.lock_contention_time, task_runtime.lock_contention_time);
}

TEST_F(ProcessProxyTest, DefaultBadMalloc) {
  auto process_proxy = MakeProcessProxy();
  process_proxy->Configure(ProcessProxyTest::DefaultOptions());
  TestTarget target(executor());
  process_proxy->Connect(IgnoreSentSignals(target.Launch()));
  FUZZING_EXPECT_OK(target.Exit(kDefaultMallocExitcode));
  FUZZING_EXPECT_OK(process_proxy->GetResult(), FuzzResult::BAD_MALLOC);
  RunUntilIdle();
}

TEST_F(ProcessProxyTest, CustomBadMalloc) {
  auto process_proxy = MakeProcessProxy();
  int32_t exitcode = 1234;
  auto options = ProcessProxyTest::DefaultOptions();
  options->set_malloc_exitcode(exitcode);
  process_proxy->Configure(options);
  TestTarget target(executor());
  process_proxy->Connect(IgnoreSentSignals(target.Launch()));
  FUZZING_EXPECT_OK(target.Exit(exitcode));
  FUZZING_EXPECT_OK(process_proxy->GetResult(), FuzzResult::BAD_MALLOC);
  RunUntilIdle();
}

TEST_F(ProcessProxyTest, DefaultDeath) {
  auto process_proxy = MakeProcessProxy();
  process_proxy->Configure(ProcessProxyTest::DefaultOptions());
  TestTarget target(executor());
  process_proxy->Connect(IgnoreSentSignals(target.Launch()));
  FUZZING_EXPECT_OK(target.Exit(kDefaultDeathExitcode));
  FUZZING_EXPECT_OK(process_proxy->GetResult(), FuzzResult::DEATH);
  RunUntilIdle();
}

TEST_F(ProcessProxyTest, CustomDeath) {
  auto process_proxy = MakeProcessProxy();
  int32_t exitcode = 4321;
  auto options = ProcessProxyTest::DefaultOptions();
  options->set_death_exitcode(exitcode);
  process_proxy->Configure(options);
  TestTarget target(executor());
  process_proxy->Connect(IgnoreSentSignals(target.Launch()));
  FUZZING_EXPECT_OK(target.Exit(exitcode));
  FUZZING_EXPECT_OK(process_proxy->GetResult(), FuzzResult::DEATH);
  RunUntilIdle();
}

TEST_F(ProcessProxyTest, Exit) {
  auto process_proxy = MakeProcessProxy();
  process_proxy->Configure(ProcessProxyTest::DefaultOptions());
  TestTarget target(executor());
  process_proxy->Connect(IgnoreSentSignals(target.Launch()));
  FUZZING_EXPECT_OK(target.Exit(1));
  FUZZING_EXPECT_OK(process_proxy->GetResult(), FuzzResult::EXIT);
  RunUntilIdle();
}

TEST_F(ProcessProxyTest, DefaultLeak) {
  auto process_proxy = MakeProcessProxy();
  process_proxy->Configure(ProcessProxyTest::DefaultOptions());
  TestTarget target(executor());
  process_proxy->Connect(IgnoreSentSignals(target.Launch()));
  FUZZING_EXPECT_OK(target.Exit(kDefaultLeakExitcode));
  FUZZING_EXPECT_OK(process_proxy->GetResult(), FuzzResult::LEAK);
  RunUntilIdle();
}

TEST_F(ProcessProxyTest, CustomLeak) {
  auto process_proxy = MakeProcessProxy();
  int32_t exitcode = 5678309;
  auto options = ProcessProxyTest::DefaultOptions();
  options->set_leak_exitcode(exitcode);
  process_proxy->Configure(options);
  TestTarget target(executor());
  process_proxy->Connect(IgnoreSentSignals(target.Launch()));
  FUZZING_EXPECT_OK(target.Exit(exitcode));
  FUZZING_EXPECT_OK(process_proxy->GetResult(), FuzzResult::LEAK);
  RunUntilIdle();
}

TEST_F(ProcessProxyTest, DefaultOom) {
  auto process_proxy = MakeProcessProxy();
  process_proxy->Configure(ProcessProxyTest::DefaultOptions());
  TestTarget target(executor());
  process_proxy->Connect(IgnoreSentSignals(target.Launch()));
  FUZZING_EXPECT_OK(target.Exit(kDefaultOomExitcode));
  FUZZING_EXPECT_OK(process_proxy->GetResult(), FuzzResult::OOM);
  RunUntilIdle();
}

TEST_F(ProcessProxyTest, CustomOom) {
  auto process_proxy = MakeProcessProxy();
  int32_t exitcode = 24601;
  auto options = ProcessProxyTest::DefaultOptions();
  options->set_oom_exitcode(exitcode);
  process_proxy->Configure(options);
  TestTarget target(executor());
  process_proxy->Connect(IgnoreSentSignals(target.Launch()));
  FUZZING_EXPECT_OK(target.Exit(exitcode));
  FUZZING_EXPECT_OK(process_proxy->GetResult(), FuzzResult::OOM);
  RunUntilIdle();
}

TEST_F(ProcessProxyTest, Timeout) {
  auto process_proxy = MakeProcessProxy();
  process_proxy->Configure(ProcessProxyTest::DefaultOptions());
  TestTarget target(executor());
  process_proxy->Connect(IgnoreSentSignals(target.Launch()));
  constexpr size_t kBufSize = 1U << 20;
  auto buf = std::make_unique<char[]>(kBufSize);
  // On timeout, the runner invokes |ProcessProxy::Dump|.
  auto len = process_proxy->Dump(buf.get(), kBufSize);
  EXPECT_GT(len, 0U);
  EXPECT_LT(len, kBufSize);
}

}  // namespace
}  // namespace fuzzing
