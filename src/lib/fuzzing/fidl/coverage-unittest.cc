// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "coverage.h"

#include <lib/gtest/test_loop_fixture.h>

#include <deque>
#include <thread>

#include "sanitizer-cov.h"
#include "test/fake-sanitizer-cov-proxy.h"

namespace fuzzing {

using fuchsia::fuzzer::CoveragePtr;

////////////////////////////////////////////////////////////////////////////////////////////////////
// Test fixture

class CoverageTest : public gtest::TestLoopFixture {
 public:
  void SetUp() override {
    TestLoopFixture::SetUp();
    FakeSanitizerCovProxy::Reset();
  }

  void Connect(CoveragePtr *proxy, zx_status_t *epitaph) {
    auto handler = aggregate_.GetHandler();
    handler(proxy->NewRequest());
    *epitaph = ZX_OK;
    proxy->set_error_handler([epitaph](zx_status_t status) { *epitaph = status; });
    RunLoopUntilIdle();
  }

  void ConnectAndAddTraces(CoveragePtr *proxy, zx_status_t *epitaph, SharedMemory *traces) {
    Connect(proxy, epitaph);
    ASSERT_EQ(traces->Create(kMaxInstructions * sizeof(Instruction)), ZX_OK);
    zx::vmo vmo;
    ASSERT_EQ(traces->Share(&vmo), ZX_OK);
    (*proxy)->AddTraces(std::move(vmo), []() {});
    RunLoopUntilIdle();
  }

  void Connect1() { Connect(&proxy1_, &epitaph1_); }
  void Connect2() { Connect(&proxy2_, &epitaph2_); }
  void ConnectAndAddTraces1() { ConnectAndAddTraces(&proxy1_, &epitaph1_, &traces1_); }
  void ConnectAndAddTraces2() { ConnectAndAddTraces(&proxy2_, &epitaph2_, &traces2_); }

 protected:
  AggregatedCoverage aggregate_;

  CoveragePtr proxy1_;
  CoveragePtr proxy2_;

  zx_status_t epitaph1_;
  zx_status_t epitaph2_;

  SharedMemory traces1_;
  SharedMemory traces2_;
};

////////////////////////////////////////////////////////////////////////////////////////////////////
// Unit tests

TEST_F(CoverageTest, AddInline8BitCounters) {
  // An invalid request disconnects.
  Connect1();

  Buffer buffer;
  size_t size = 0;
  ASSERT_EQ(zx::vmo::create(size, 0, &buffer.vmo), ZX_OK);
  buffer.size = size;
  proxy1_->AddInline8BitCounters(std::move(buffer), []() {});

  RunLoopUntilIdle();
  EXPECT_EQ(epitaph1_, ZX_ERR_INVALID_ARGS);
  EXPECT_FALSE(FakeSanitizerCovProxy::HasInit(size));

  // Make a valid request
  Connect2();
  size = 0x1001 * sizeof(uint8_t);
  ASSERT_EQ(zx::vmo::create(size, 0, &buffer.vmo), ZX_OK);
  buffer.size = size;
  proxy2_->AddInline8BitCounters(std::move(buffer), []() {});

  RunLoopUntilIdle();
  EXPECT_EQ(epitaph2_, ZX_OK);
  EXPECT_TRUE(FakeSanitizerCovProxy::HasInit(size));
}

TEST_F(CoverageTest, AddPcTable) {
  // An invalid request disconnects.
  Connect1();

  Buffer buffer;
  size_t size = 0;
  ASSERT_EQ(zx::vmo::create(size, 0, &buffer.vmo), ZX_OK);
  buffer.size = size;
  proxy1_->AddPcTable(std::move(buffer), []() {});

  RunLoopUntilIdle();
  EXPECT_EQ(epitaph1_, ZX_ERR_INVALID_ARGS);
  EXPECT_FALSE(FakeSanitizerCovProxy::HasInit(size));

  // Make a valid request
  Connect2();
  size = 0x2002 * sizeof(uintptr_t);
  ASSERT_EQ(zx::vmo::create(size, 0, &buffer.vmo), ZX_OK);
  buffer.size = size;
  proxy2_->AddPcTable(std::move(buffer), []() {});

  RunLoopUntilIdle();
  EXPECT_EQ(epitaph2_, ZX_OK);
  EXPECT_TRUE(FakeSanitizerCovProxy::HasInit(size));
}

TEST_F(CoverageTest, AddTraces) {
  // An invalid request disconnects (invalid size).
  Connect1();

  zx::vmo vmo;
  ASSERT_EQ(zx::vmo::create(sizeof(Instruction) * kInstructionBufferLen, 0, &vmo), ZX_OK);
  proxy1_->AddTraces(std::move(vmo), []() {});

  RunLoopUntilIdle();
  EXPECT_EQ(epitaph1_, ZX_ERR_BUFFER_TOO_SMALL);

  // Make a valid request
  Connect2();

  ASSERT_EQ(zx::vmo::create(sizeof(Instruction) * kMaxInstructions, 0, &vmo), ZX_OK);
  proxy2_->AddTraces(std::move(vmo), []() {});

  RunLoopUntilIdle();
  EXPECT_EQ(epitaph2_, ZX_OK);
}

TEST_F(CoverageTest, ProcessAll) {
  // Fill with some instruction data.
  ConnectAndAddTraces1();
  Instruction *traces = traces1_.begin<Instruction>();
  for (size_t i = 0; i < kMaxInstructions; ++i) {
    traces[i].type = Instruction::kCmp8;
    traces[i].pc = 0x1000;
    traces[i].args[0] = 1;
    traces[i].args[1] = 2;
  }
  EXPECT_EQ(traces1_.vmo().signal(kWritableSignalA, kReadableSignalA), ZX_OK);
  EXPECT_EQ(traces1_.vmo().wait_one(kWritableSignalA, zx::time::infinite(), nullptr), ZX_OK);
  EXPECT_EQ(FakeSanitizerCovProxy::Count(Instruction::kCmp8, 0x1000, 1, 2), kInstructionBufferLen);

  EXPECT_EQ(traces1_.vmo().signal(kWritableSignalB, kReadableSignalB), ZX_OK);
  EXPECT_EQ(traces1_.vmo().wait_one(kWritableSignalB, zx::time::infinite(), nullptr), ZX_OK);
  EXPECT_EQ(FakeSanitizerCovProxy::Count(Instruction::kCmp8, 0x1000, 1, 2),
            kInstructionBufferLen * 2);

  // Add a second proxy while tracing.
  ConnectAndAddTraces2();
  traces = traces2_.begin<Instruction>();
  for (size_t i = 0; i < kMaxInstructions; ++i) {
    traces[i].type = Instruction::kCmp4;
    traces[i].pc = 0x2000;
    traces[i].args[0] = 3;
    traces[i].args[1] = 4;
  }
  EXPECT_EQ(traces1_.vmo().signal(kWritableSignalA, kReadableSignalA), ZX_OK);
  EXPECT_EQ(traces2_.vmo().signal(kWritableSignalA, kReadableSignalA), ZX_OK);
  EXPECT_EQ(traces1_.vmo().wait_one(kWritableSignalA, zx::time::infinite(), nullptr), ZX_OK);
  EXPECT_EQ(traces2_.vmo().wait_one(kWritableSignalA, zx::time::infinite(), nullptr), ZX_OK);
  EXPECT_EQ(FakeSanitizerCovProxy::Count(Instruction::kCmp8, 0x1000, 1, 2),
            kInstructionBufferLen * 3);
  EXPECT_EQ(FakeSanitizerCovProxy::Count(Instruction::kCmp4, 0x2000, 3, 4), kInstructionBufferLen);

  // Remove a proxy while tracing.
  proxy1_.Unbind();
  EXPECT_EQ(traces2_.vmo().signal(kWritableSignalB, kReadableSignalB), ZX_OK);
  EXPECT_EQ(traces2_.vmo().wait_one(kWritableSignalB, zx::time::infinite(), nullptr), ZX_OK);
  EXPECT_EQ(FakeSanitizerCovProxy::Count(Instruction::kCmp8, 0x1000, 1, 2),
            kInstructionBufferLen * 3);
  EXPECT_EQ(FakeSanitizerCovProxy::Count(Instruction::kCmp4, 0x2000, 3, 4),
            kInstructionBufferLen * 2);
}

TEST_F(CoverageTest, CompleteIteration) {
  // Completing an itertion should result in all proxies writing a sentinel, and then being able to
  // continue.
  ConnectAndAddTraces1();
  Instruction *traces = traces1_.begin<Instruction>();
  for (size_t i = 0; i < 20; ++i) {
    traces[i].type = Instruction::kCmp2;
    traces[i].pc = 0x3333;
    traces[i].args[0] = 5;
    traces[i].args[1] = 6;
  }
  traces[20].type = Instruction::kSentinel;
  traces[20].pc = 0;
  traces[20].args[0] = 0;
  traces[20].args[1] = 0;

  std::thread t1([this]() {
    traces1_.vmo().wait_one(kBetweenIterations, zx::time::infinite(), nullptr);
    traces1_.vmo().signal(kWritableSignalA, kReadableSignalA);
    traces1_.vmo().wait_one(kInIteration, zx::time::infinite(), nullptr);
  });

  EXPECT_EQ(aggregate_.CompleteIteration(), ZX_OK);
  EXPECT_EQ(FakeSanitizerCovProxy::Count(Instruction::kCmp2, 0x3333, 5, 6), 20u);
  t1.join();
}

}  // namespace fuzzing
