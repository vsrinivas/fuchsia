// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/sys/fuzzing/realmfuzzer/engine/process-proxy.h"

#include <lib/sync/completion.h>

#include <sstream>

#include <gtest/gtest.h>

#include "src/sys/fuzzing/common/options.h"
#include "src/sys/fuzzing/realmfuzzer/engine/process-proxy-test.h"
#include "src/sys/fuzzing/realmfuzzer/target/module.h"
#include "src/sys/fuzzing/realmfuzzer/testing/module.h"
#include "src/sys/fuzzing/realmfuzzer/testing/target.h"

namespace fuzzing {
namespace {

// Unit tests.

TEST_F(ProcessProxyTest, Connect) {
  TestTarget target(executor());
  auto process = target.Launch();
  zx_info_handle_basic_t info;
  EXPECT_EQ(process.get_info(ZX_INFO_HANDLE_BASIC, &info, sizeof(info), nullptr, nullptr), ZX_OK);
  auto process_proxy = CreateAndConnectProxy(std::move(process));
  EXPECT_EQ(process_proxy->target_id(), info.koid);
}

TEST_F(ProcessProxyTest, AddLlvmModule) {
  TestTarget target(executor());
  AsyncEventPair eventpair(executor());
  auto process_proxy = CreateAndConnectProxy(target.Launch(), eventpair.Create());

  FakeRealmFuzzerModule module;
  zx::vmo inline_8bit_counters;

  // Invalid id.
  EXPECT_EQ(module.Share(0x1234, &inline_8bit_counters), ZX_OK);
  const char* invalid_name = "invalid";
  EXPECT_EQ(inline_8bit_counters.set_property(ZX_PROP_NAME, invalid_name, strlen(invalid_name)),
            ZX_OK);
  EXPECT_EQ(process_proxy->AddModule(inline_8bit_counters), ZX_ERR_INVALID_ARGS);

  // Valid.
  EXPECT_EQ(module.Share(0x1234, &inline_8bit_counters), ZX_OK);
  EXPECT_EQ(process_proxy->AddModule(inline_8bit_counters), ZX_OK);

  // Adding a duplicate module fails.
  EXPECT_EQ(module.Share(0x1234, &inline_8bit_counters), ZX_OK);
  EXPECT_EQ(process_proxy->AddModule(inline_8bit_counters), ZX_ERR_ALREADY_BOUND);

  // Coverage should be reflected in the pool.
  auto* module_impl = pool()->Get(module.id(), module.num_pcs());
  EXPECT_EQ(module_impl->Measure(), 0U);
  module[0] = 1;
  module[1] = 4;
  module[2] = 8;
  module.Update();
  EXPECT_EQ(module_impl->Measure(), 3U);
}

TEST_F(ProcessProxyTest, Signals) {
  TestTarget target(executor());
  zx::vmo data;
  AsyncEventPair eventpair(executor());
  auto process_proxy = CreateAndConnectProxy(target.Launch(), eventpair.Create());

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
  TestTarget target(executor());
  auto process_proxy = CreateAndConnectProxy(target.Launch());
  ProcessStats stats;
  EXPECT_EQ(process_proxy->GetStats(&stats), ZX_OK);
  EXPECT_EQ(stats.koid, process_proxy->target_id());
}

TEST_F(ProcessProxyTest, DefaultBadMalloc) {
  TestTarget target(executor());
  auto process_proxy = CreateAndConnectProxy(target.Launch());
  FUZZING_EXPECT_OK(target.Exit(kDefaultMallocExitcode));
  FUZZING_EXPECT_OK(process_proxy->GetResult(), FuzzResult::BAD_MALLOC);
  RunUntilIdle();
}

TEST_F(ProcessProxyTest, CustomBadMalloc) {
  int32_t exitcode = 1234;
  auto options = MakeOptions();
  options->set_malloc_exitcode(exitcode);
  TestTarget target(executor());
  auto process_proxy = CreateAndConnectProxy(target.Launch(), options);
  FUZZING_EXPECT_OK(target.Exit(exitcode));
  FUZZING_EXPECT_OK(process_proxy->GetResult(), FuzzResult::BAD_MALLOC);
  RunUntilIdle();
}

TEST_F(ProcessProxyTest, DefaultDeath) {
  TestTarget target(executor());
  auto process_proxy = CreateAndConnectProxy(target.Launch());
  FUZZING_EXPECT_OK(target.Exit(kDefaultDeathExitcode));
  FUZZING_EXPECT_OK(process_proxy->GetResult(), FuzzResult::DEATH);
  RunUntilIdle();
}

TEST_F(ProcessProxyTest, CustomDeath) {
  int32_t exitcode = 4321;
  auto options = MakeOptions();
  options->set_death_exitcode(exitcode);
  TestTarget target(executor());
  auto process_proxy = CreateAndConnectProxy(target.Launch(), options);
  FUZZING_EXPECT_OK(target.Exit(exitcode));
  FUZZING_EXPECT_OK(process_proxy->GetResult(), FuzzResult::DEATH);
  RunUntilIdle();
}

TEST_F(ProcessProxyTest, Exit) {
  TestTarget target(executor());
  auto process_proxy = CreateAndConnectProxy(target.Launch());
  FUZZING_EXPECT_OK(target.Exit(1));
  FUZZING_EXPECT_OK(process_proxy->GetResult(), FuzzResult::EXIT);
  RunUntilIdle();
}

TEST_F(ProcessProxyTest, DefaultLeak) {
  TestTarget target(executor());
  auto process_proxy = CreateAndConnectProxy(target.Launch());
  FUZZING_EXPECT_OK(target.Exit(kDefaultLeakExitcode));
  FUZZING_EXPECT_OK(process_proxy->GetResult(), FuzzResult::LEAK);
  RunUntilIdle();
}

TEST_F(ProcessProxyTest, CustomLeak) {
  int32_t exitcode = 5678309;
  auto options = MakeOptions();
  options->set_leak_exitcode(exitcode);
  TestTarget target(executor());
  auto process_proxy = CreateAndConnectProxy(target.Launch(), options);
  FUZZING_EXPECT_OK(target.Exit(exitcode));
  FUZZING_EXPECT_OK(process_proxy->GetResult(), FuzzResult::LEAK);
  RunUntilIdle();
}

TEST_F(ProcessProxyTest, DefaultOom) {
  TestTarget target(executor());
  auto process_proxy = CreateAndConnectProxy(target.Launch());
  FUZZING_EXPECT_OK(target.Exit(kDefaultOomExitcode));
  FUZZING_EXPECT_OK(process_proxy->GetResult(), FuzzResult::OOM);
  RunUntilIdle();
}

TEST_F(ProcessProxyTest, CustomOom) {
  int32_t exitcode = 24601;
  auto options = MakeOptions();
  options->set_oom_exitcode(exitcode);
  TestTarget target(executor());
  auto process_proxy = CreateAndConnectProxy(target.Launch(), options);
  FUZZING_EXPECT_OK(target.Exit(exitcode));
  FUZZING_EXPECT_OK(process_proxy->GetResult(), FuzzResult::OOM);
  RunUntilIdle();
}

TEST_F(ProcessProxyTest, Timeout) {
  TestTarget target(executor());
  auto process_proxy = CreateAndConnectProxy(target.Launch());
  constexpr size_t kBufSize = 1U << 20;
  auto buf = std::make_unique<char[]>(kBufSize);
  // On timeout, the runner invokes |ProcessProxy::Dump|.
  auto len = process_proxy->Dump(buf.get(), kBufSize);
  EXPECT_GT(len, 0U);
  EXPECT_LT(len, kBufSize);
}

}  // namespace
}  // namespace fuzzing
