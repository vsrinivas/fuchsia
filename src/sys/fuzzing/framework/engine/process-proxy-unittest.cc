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
  EXPECT_EQ(process_proxy->Connect(IgnoreAll()), ZX_OK);
}

TEST_F(ProcessProxyTest, AddFeedback) {
  auto process_proxy = MakeProcessProxy();

  FakeModule fake;
  fake[0] = 1;
  fake[1] = 4;
  fake[2] = 8;

  // Add before connecting.
  Module module1(fake.counters(), fake.pcs(), fake.num_pcs());
  auto llvm_module1 = module1.GetLlvmModule();
  EXPECT_EQ(process_proxy->AddLlvmModule(std::move(llvm_module1)), ZX_ERR_PEER_CLOSED);

  // Add after connecting.
  Module module2(fake.counters(), fake.pcs(), fake.num_pcs());
  EXPECT_EQ(process_proxy->Connect(IgnoreAll()), ZX_OK);
  auto llvm_module2 = module2.GetLlvmModule();
  EXPECT_EQ(process_proxy->AddLlvmModule(std::move(llvm_module2)), ZX_OK);

  auto* module_impl = pool()->Get(module2.id(), fake.num_pcs());
  EXPECT_EQ(module_impl->Measure(), 3U);
}

TEST_F(ProcessProxyTest, Signals) {
  auto process_proxy = MakeProcessProxy();
  process_proxy->Configure(ProcessProxyTest::DefaultOptions());

  AsyncEventPair eventpair(executor());
  EXPECT_EQ(process_proxy->Connect(IgnoreTarget(eventpair.Create())), ZX_OK);
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
  EXPECT_EQ(process_proxy->Finish(), ZX_OK);
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
  EXPECT_EQ(process_proxy->Finish(), ZX_OK);
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
  zx_info_handle_basic_t info;
  EXPECT_EQ(spawned.get_info(ZX_INFO_HANDLE_BASIC, &info, sizeof(info), nullptr, nullptr), ZX_OK);

  EXPECT_EQ(process_proxy->Connect(IgnoreSentSignals(std::move(spawned))), ZX_OK);
  ProcessStats stats;
  EXPECT_EQ(process_proxy->GetStats(&stats), ZX_OK);
  EXPECT_EQ(stats.koid, info.koid);

  // The kernel stats are a bit jittery when requested in quick succession. Just check that some
  // data was received.
  EXPECT_NE(stats.mem_mapped_bytes, 0U);
  EXPECT_NE(stats.mem_private_bytes, 0U);
  EXPECT_NE(stats.cpu_time, 0U);
}

TEST_F(ProcessProxyTest, DefaultBadMalloc) {
  auto process_proxy = MakeProcessProxy();
  process_proxy->Configure(ProcessProxyTest::DefaultOptions());
  TestTarget target(executor());
  EXPECT_EQ(process_proxy->Connect(IgnoreSentSignals(target.Launch())), ZX_OK);
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
  EXPECT_EQ(process_proxy->Connect(IgnoreSentSignals(target.Launch())), ZX_OK);
  FUZZING_EXPECT_OK(target.Exit(exitcode));
  FUZZING_EXPECT_OK(process_proxy->GetResult(), FuzzResult::BAD_MALLOC);
  RunUntilIdle();
}

TEST_F(ProcessProxyTest, DefaultDeath) {
  auto process_proxy = MakeProcessProxy();
  process_proxy->Configure(ProcessProxyTest::DefaultOptions());
  TestTarget target(executor());
  EXPECT_EQ(process_proxy->Connect(IgnoreSentSignals(target.Launch())), ZX_OK);
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
  EXPECT_EQ(process_proxy->Connect(IgnoreSentSignals(target.Launch())), ZX_OK);
  FUZZING_EXPECT_OK(target.Exit(exitcode));
  FUZZING_EXPECT_OK(process_proxy->GetResult(), FuzzResult::DEATH);
  RunUntilIdle();
}

TEST_F(ProcessProxyTest, Exit) {
  auto process_proxy = MakeProcessProxy();
  process_proxy->Configure(ProcessProxyTest::DefaultOptions());
  TestTarget target(executor());
  EXPECT_EQ(process_proxy->Connect(IgnoreSentSignals(target.Launch())), ZX_OK);
  FUZZING_EXPECT_OK(target.Exit(1));
  FUZZING_EXPECT_OK(process_proxy->GetResult(), FuzzResult::EXIT);
  RunUntilIdle();
}

TEST_F(ProcessProxyTest, DefaultLeak) {
  auto process_proxy = MakeProcessProxy();
  process_proxy->Configure(ProcessProxyTest::DefaultOptions());
  TestTarget target(executor());
  EXPECT_EQ(process_proxy->Connect(IgnoreSentSignals(target.Launch())), ZX_OK);
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
  EXPECT_EQ(process_proxy->Connect(IgnoreSentSignals(target.Launch())), ZX_OK);
  FUZZING_EXPECT_OK(target.Exit(exitcode));
  FUZZING_EXPECT_OK(process_proxy->GetResult(), FuzzResult::LEAK);
  RunUntilIdle();
}

TEST_F(ProcessProxyTest, DefaultOom) {
  auto process_proxy = MakeProcessProxy();
  process_proxy->Configure(ProcessProxyTest::DefaultOptions());
  TestTarget target(executor());
  EXPECT_EQ(process_proxy->Connect(IgnoreSentSignals(target.Launch())), ZX_OK);
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
  EXPECT_EQ(process_proxy->Connect(IgnoreSentSignals(target.Launch())), ZX_OK);
  FUZZING_EXPECT_OK(target.Exit(exitcode));
  FUZZING_EXPECT_OK(process_proxy->GetResult(), FuzzResult::OOM);
  RunUntilIdle();
}

TEST_F(ProcessProxyTest, Timeout) {
  auto process_proxy = MakeProcessProxy();
  process_proxy->Configure(ProcessProxyTest::DefaultOptions());
  TestTarget target(executor());
  EXPECT_EQ(process_proxy->Connect(IgnoreSentSignals(target.Launch())), ZX_OK);
  constexpr size_t kBufSize = 1U << 20;
  auto buf = std::make_unique<char[]>(kBufSize);
  // On timeout, the runner invokes |ProcessProxy::Dump|.
  auto len = process_proxy->Dump(buf.get(), kBufSize);
  EXPECT_GT(len, 0U);
  EXPECT_LT(len, kBufSize);
}

}  // namespace
}  // namespace fuzzing
