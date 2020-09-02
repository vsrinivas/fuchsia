// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "sanitizer-cov-proxy.h"

#include <lib/gtest/test_loop_fixture.h>
#include <lib/sys/cpp/testing/component_context_provider.h>
#include <lib/syslog/cpp/macros.h>
#include <stddef.h>
#include <string.h>

#include <thread>

#include "libfuzzer.h"
#include "sanitizer-cov.h"
#include "test/fake-coverage.h"
#include "test/fake-libfuzzer.h"

namespace fuzzing {

using ::fuchsia::fuzzer::Coverage;

////////////////////////////////////////////////////////////////////////////////////////////////////
// Test fixture

class SanitizerCovProxyTest : public gtest::TestLoopFixture {
 public:
  void SetUp() override {
    TestLoopFixture::SetUp();
    context_ = provider_.TakeContext();
    context_->outgoing()->AddPublicService(coverage_.GetHandler());

    CoveragePtr coverage;
    provider_.ConnectToPublicService(coverage.NewRequest());

    auto proxy = SanitizerCovProxy::GetInstance(false /* autoconnect */);
    ASSERT_EQ(proxy->SetCoverage(std::move(coverage)), ZX_OK);
    coverage_.Configure();
    RunLoopUntilIdle();
  }

 protected:
  FakeCoverage coverage_;

 private:
  sys::testing::ComponentContextProvider provider_;
  std::unique_ptr<sys::ComponentContext> context_;
};

}  // namespace fuzzing

////////////////////////////////////////////////////////////////////////////////////////////////////
// Unit tests

namespace fuzzing {

TEST_F(SanitizerCovProxyTest, AddInline8BitCounters) {
  const size_t kBufferSize1 = 0x1000;
  uint8_t buffer1[kBufferSize1];

  // Invalid arguments are ignored
  __sanitizer_cov_8bit_counters_init(nullptr, buffer1 + kBufferSize1);
  __sanitizer_cov_8bit_counters_init(buffer1, nullptr);
  __sanitizer_cov_8bit_counters_init(buffer1 + kBufferSize1, buffer1);

  SharedMemory shmem;
  EXPECT_FALSE(coverage_.MapPending(&shmem));

  // Valid
  // This needs a separate thread so the test can still drive the loop.
  sync_completion_t sync;
  std::thread t1([&buffer1, &sync]() {
    __sanitizer_cov_8bit_counters_init(buffer1, buffer1 + kBufferSize1);
    sync_completion_signal(&sync);
  });

  // Keep prodding the test loop until the thread above is done making FIDL requests.
  zx_status_t status;
  while ((status = sync_completion_wait(&sync, ZX_MSEC(10))) == ZX_ERR_TIMED_OUT) {
    RunLoopUntilIdle();
  }
  ASSERT_EQ(status, ZX_OK);
  t1.join();

  EXPECT_TRUE(coverage_.MapPending(&shmem));
  EXPECT_EQ(shmem.len(), kBufferSize1 * sizeof(uint8_t));

  // Should be able to do another, different region
  // This needs a separate thread so the test can still drive the loop.
  const size_t kBufferSize2 = 0x2000;
  uint8_t buffer2[kBufferSize2];
  sync_completion_reset(&sync);
  std::thread t2([&buffer2, &sync]() {
    __sanitizer_cov_8bit_counters_init(buffer2, buffer2 + kBufferSize2);
    sync_completion_signal(&sync);
  });

  // Keep prodding the test loop until the thread above is done making FIDL requests.
  while ((status = sync_completion_wait(&sync, ZX_MSEC(10))) == ZX_ERR_TIMED_OUT) {
    RunLoopUntilIdle();
  }
  ASSERT_EQ(status, ZX_OK);
  t2.join();

  EXPECT_TRUE(coverage_.MapPending(&shmem));
  EXPECT_EQ(shmem.len(), kBufferSize2 * sizeof(uint8_t));
}

TEST_F(SanitizerCovProxyTest, AddPcTable) {
  const size_t kBufferSize1 = 0x1000;
  uintptr_t buffer1[kBufferSize1];

  // Invalid arguments are ignored
  __sanitizer_cov_pcs_init(nullptr, buffer1 + kBufferSize1);
  __sanitizer_cov_pcs_init(buffer1, nullptr);
  __sanitizer_cov_pcs_init(buffer1 + kBufferSize1, buffer1);

  SharedMemory shmem;
  EXPECT_FALSE(coverage_.MapPending(&shmem));

  // Valid
  // This needs a separate thread so the test can still drive the loop.
  sync_completion_t sync;
  std::thread t1([&buffer1, &sync]() {
    __sanitizer_cov_pcs_init(buffer1, buffer1 + kBufferSize1);
    sync_completion_signal(&sync);
  });

  // Keep prodding the test loop until the thread above is done making FIDL requests.
  zx_status_t status;
  while ((status = sync_completion_wait(&sync, ZX_MSEC(10))) == ZX_ERR_TIMED_OUT) {
    RunLoopUntilIdle();
  }
  ASSERT_EQ(status, ZX_OK);
  t1.join();

  EXPECT_TRUE(coverage_.MapPending(&shmem));
  EXPECT_EQ(shmem.len(), kBufferSize1 * sizeof(uintptr_t));

  // Should be able to do another, different region
  // This needs a separate thread so the test can still drive the loop.
  const size_t kBufferSize2 = 0x2000;
  uintptr_t buffer2[kBufferSize2];
  sync_completion_reset(&sync);
  std::thread t2([&buffer2, &sync]() {
    __sanitizer_cov_pcs_init(buffer2, buffer2 + kBufferSize2);
    sync_completion_signal(&sync);
  });

  // Keep prodding the test loop until the thread above is done making FIDL requests.
  while ((status = sync_completion_wait(&sync, ZX_MSEC(10))) == ZX_ERR_TIMED_OUT) {
    RunLoopUntilIdle();
  }
  ASSERT_EQ(status, ZX_OK);
  t2.join();

  EXPECT_TRUE(coverage_.MapPending(&shmem));
  EXPECT_EQ(shmem.len(), kBufferSize2 * sizeof(uintptr_t));
}

TEST_F(SanitizerCovProxyTest, AddTrace) {
  uintptr_t pc = 0x1000;

  LLVMFuzzerSetRemoteCallerPC(++pc);
  __sanitizer_cov_trace_pc_indir(~pc);
  Instruction *trace = coverage_.traces();

  EXPECT_EQ(trace->type, Instruction::kPcIndir);
  EXPECT_EQ(trace->pc, pc);
  EXPECT_EQ(trace->args[0], ~pc);
  EXPECT_EQ(trace->args[1], 0u);

  LLVMFuzzerSetRemoteCallerPC(++pc);
  __sanitizer_cov_trace_cmp8((uint64_t)1, (uint64_t)-1);
  trace++;

  EXPECT_EQ(trace->type, Instruction::kCmp8);
  EXPECT_EQ(trace->pc, pc);
  EXPECT_EQ(trace->args[0], 1u);
  EXPECT_EQ(trace->args[1], 0xffffffffffffffff);

  LLVMFuzzerSetRemoteCallerPC(++pc);
  __sanitizer_cov_trace_const_cmp8((uint64_t)2, (uint64_t)-2);
  trace++;

  EXPECT_EQ(trace->type, Instruction::kConstCmp8);
  EXPECT_EQ(trace->pc, pc);
  EXPECT_EQ(trace->args[0], 2u);
  EXPECT_EQ(trace->args[1], 0xfffffffffffffffe);

  LLVMFuzzerSetRemoteCallerPC(++pc);
  __sanitizer_cov_trace_cmp4((uint32_t)3, (uint32_t)-3);
  trace++;

  EXPECT_EQ(trace->type, Instruction::kCmp4);
  EXPECT_EQ(trace->pc, pc);
  EXPECT_EQ(trace->args[0], 3u);
  EXPECT_EQ(trace->args[1], 0xfffffffd);

  LLVMFuzzerSetRemoteCallerPC(++pc);
  __sanitizer_cov_trace_const_cmp4((uint32_t)4, (uint32_t)-4);
  trace++;

  EXPECT_EQ(trace->type, Instruction::kConstCmp4);
  EXPECT_EQ(trace->pc, pc);
  EXPECT_EQ(trace->args[0], 4u);
  EXPECT_EQ(trace->args[1], 0xfffffffc);

  LLVMFuzzerSetRemoteCallerPC(++pc);
  __sanitizer_cov_trace_cmp2((uint16_t)5, (uint16_t)-5);
  trace++;

  EXPECT_EQ(trace->type, Instruction::kCmp2);
  EXPECT_EQ(trace->pc, pc);
  EXPECT_EQ(trace->args[0], 5u);
  EXPECT_EQ(trace->args[1], 0xfffbu);

  LLVMFuzzerSetRemoteCallerPC(++pc);
  __sanitizer_cov_trace_const_cmp2((uint16_t)6, (uint16_t)-6);
  trace++;

  EXPECT_EQ(trace->type, Instruction::kConstCmp2);
  EXPECT_EQ(trace->pc, pc);
  EXPECT_EQ(trace->args[0], 6u);
  EXPECT_EQ(trace->args[1], 0xfffau);

  LLVMFuzzerSetRemoteCallerPC(++pc);
  __sanitizer_cov_trace_cmp1((uint8_t)7, (uint8_t)-7);
  trace++;

  EXPECT_EQ(trace->type, Instruction::kCmp1);
  EXPECT_EQ(trace->pc, pc);
  EXPECT_EQ(trace->args[0], 7u);
  EXPECT_EQ(trace->args[1], 0xf9u);

  LLVMFuzzerSetRemoteCallerPC(++pc);
  __sanitizer_cov_trace_const_cmp1((uint8_t)8, (uint8_t)-8);
  trace++;

  EXPECT_EQ(trace->type, Instruction::kConstCmp1);
  EXPECT_EQ(trace->pc, pc);
  EXPECT_EQ(trace->args[0], 8u);
  EXPECT_EQ(trace->args[1], 0xf8u);

  LLVMFuzzerSetRemoteCallerPC(++pc);
  __sanitizer_cov_trace_div4((uint32_t)-9);
  trace++;

  EXPECT_EQ(trace->type, Instruction::kDiv4);
  EXPECT_EQ(trace->pc, pc);
  EXPECT_EQ(trace->args[0], 0xfffffff7);
  EXPECT_EQ(trace->args[1], 0u);

  LLVMFuzzerSetRemoteCallerPC(++pc);
  __sanitizer_cov_trace_div8((uint64_t)-10);
  trace++;

  EXPECT_EQ(trace->type, Instruction::kDiv8);
  EXPECT_EQ(trace->pc, pc);
  EXPECT_EQ(trace->args[0], 0xfffffffffffffff6);
  EXPECT_EQ(trace->args[1], 0u);

  LLVMFuzzerSetRemoteCallerPC(++pc);
  __sanitizer_cov_trace_gep((uintptr_t)11);
  trace++;

  EXPECT_EQ(trace->type, Instruction::kGep);
  EXPECT_EQ(trace->pc, pc);
  EXPECT_EQ(trace->args[0], 11u);
  EXPECT_EQ(trace->args[1], 0u);
}

TEST_F(SanitizerCovProxyTest, AddTraceSwitch) {
  uintptr_t pc = 0x2000;
  std::vector<uint64_t> cases;
  Instruction *trace = coverage_.traces();

  // Invalid number of cases. This shouldn't add a trace.
  cases = std::vector<uint64_t>{0, 64};
  LLVMFuzzerSetRemoteCallerPC(++pc);
  __sanitizer_cov_trace_switch(257, &cases[0]);
  EXPECT_EQ(trace->type, Instruction::kInvalid);

  // Invalid number of bits. This shouldn't add a trace.
  cases = std::vector<uint64_t>{2, 63, 0, 258};
  LLVMFuzzerSetRemoteCallerPC(++pc);
  __sanitizer_cov_trace_switch(257, &cases[0]);
  EXPECT_EQ(trace->type, Instruction::kInvalid);

  // Small values (<256) don't get recorded
  cases = std::vector<uint64_t>{2, 64, 0, 258};
  LLVMFuzzerSetRemoteCallerPC(++pc);
  __sanitizer_cov_trace_switch(255, &cases[0]);
  EXPECT_EQ(trace->type, Instruction::kInvalid);

  // Small cases (all < 256) don't get recorded
  cases = std::vector<uint64_t>{2, 64, 0, 255};
  LLVMFuzzerSetRemoteCallerPC(++pc);
  __sanitizer_cov_trace_switch(257, &cases[0]);
  EXPECT_EQ(trace->type, Instruction::kInvalid);

  // Should be able to trace switch with a single case. Try less than, equal to, and greater then.
  cases = std::vector<uint64_t>{1, 32, 258};
  LLVMFuzzerSetRemoteCallerPC(++pc);
  __sanitizer_cov_trace_switch(257, &cases[0]);

  EXPECT_EQ(trace->type, Instruction::kCmp4);
  EXPECT_EQ(trace->args[0], 257u);
  EXPECT_EQ(trace->args[1], 0u);

  trace++;
  EXPECT_EQ(trace->type, Instruction::kCmp4);
  EXPECT_EQ(trace->args[0], 257u);
  EXPECT_EQ(trace->args[1], 258u);

  LLVMFuzzerSetRemoteCallerPC(++pc);
  __sanitizer_cov_trace_switch(258, &cases[0]);

  trace++;
  EXPECT_EQ(trace->type, Instruction::kCmp4);
  EXPECT_EQ(trace->args[0], 258u);
  EXPECT_EQ(trace->args[1], 0u);

  trace++;
  EXPECT_EQ(trace->type, Instruction::kCmp4);
  EXPECT_EQ(trace->args[0], 258u);
  EXPECT_EQ(trace->args[1], 0xffffffff);

  LLVMFuzzerSetRemoteCallerPC(++pc);
  __sanitizer_cov_trace_switch(259, &cases[0]);

  trace++;
  EXPECT_EQ(trace->type, Instruction::kCmp4);
  EXPECT_EQ(trace->args[0], 259u);
  EXPECT_EQ(trace->args[1], 258u);

  trace++;
  EXPECT_EQ(trace->type, Instruction::kCmp4);
  EXPECT_EQ(trace->args[0], 259u);
  EXPECT_EQ(trace->args[1], 0xffffffff);

  // Should able to handle multiple cases. Make sure correct adjacent cases are selected.
  cases = std::vector<uint64_t>{4, 16, 0x1011, 0x1012, 0x1013, 0x1014};
  LLVMFuzzerSetRemoteCallerPC(++pc);
  __sanitizer_cov_trace_switch(0x1012, &cases[0]);

  trace++;
  EXPECT_EQ(trace->type, Instruction::kCmp2);
  EXPECT_EQ(trace->args[0], 0x1012u);
  EXPECT_EQ(trace->args[1], 0x1011u);

  trace++;
  EXPECT_EQ(trace->type, Instruction::kCmp2);
  EXPECT_EQ(trace->args[0], 0x1012u);
  EXPECT_EQ(trace->args[1], 0x1013u);
}

TEST_F(SanitizerCovProxyTest, AddTrace_MultiThreaded) {
  Instruction *trace = coverage_.traces();

  std::thread t1([]() {
    for (size_t i = 0; i < kInstructionBufferLen / 2; i++) {
      __sanitizer_cov_trace_pc_indir(0x1000);
      zx::nanosleep(zx::deadline_after(zx::nsec(1)));
    }
  });

  std::thread t2([]() {
    for (size_t i = 0; i < kInstructionBufferLen / 4; i++) {
      __sanitizer_cov_trace_gep(0x7fff);
      zx::nanosleep(zx::deadline_after(zx::nsec(1)));
    }
  });

  // Neither thread should block.
  t1.join();
  t2.join();

  // The order of traces should be unpredictable, but the totla number of each should be consistent.
  size_t num_pc_indirs = 0;
  size_t num_geps = 0;
  while (true) {
    switch (trace->type) {
      case Instruction::kPcIndir:
        EXPECT_EQ(trace->args[0], 0x1000u);
        num_pc_indirs++;
        break;
      case Instruction::kGep:
        EXPECT_EQ(trace->args[0], 0x7fffu);
        num_geps++;
        break;
      case Instruction::kInvalid:
        EXPECT_EQ(num_pc_indirs, kInstructionBufferLen / 2);
        EXPECT_EQ(num_geps, kInstructionBufferLen / 4);
        return;
      default:
        FX_NOTREACHED();
    }
    trace++;
  }
}

TEST_F(SanitizerCovProxyTest, AddTrace_ExceedThreshold) {
  // Trace more than |kInstructionBufferLen| instructions without resolution. This should force an
  // update request to be sent.
  for (size_t i = 0; i < kInstructionBufferLen + 1; i++) {
    __sanitizer_cov_trace_pc_indir(0x1000);
  }

  // ...but it should result in an update request.
  coverage_.Resolve();
  EXPECT_EQ(coverage_.Count(Instruction::kPcIndir), kInstructionBufferLen);
}

TEST_F(SanitizerCovProxyTest, AddTrace_ExhaustUnresolved) {
  // Trace more than |kMaxInstructions| instructions without resolution. This should cause some
  // calls to block until the Coverage service can free up some of the trace array.
  sync_completion_t sync;
  std::thread t1([&sync]() {
    for (size_t i = 0; i < kMaxInstructions + 1; i++) {
      __sanitizer_cov_trace_pc_indir(0x1000);
      __sanitizer_cov_trace_gep(0x7fff);
    }
    sync_completion_signal(&sync);
  });

  // Keep prodding the test loop until the thread above is done making FIDL requests.
  zx_status_t status;
  while ((status = sync_completion_wait(&sync, ZX_MSEC(10))) == ZX_ERR_TIMED_OUT) {
    coverage_.Resolve();
  }
  ASSERT_EQ(status, ZX_OK);
  t1.join();
  coverage_.Resolve();

  EXPECT_EQ(coverage_.Count(Instruction::kPcIndir), kMaxInstructions);
  EXPECT_EQ(coverage_.Count(Instruction::kGep), kMaxInstructions);
}

TEST_F(SanitizerCovProxyTest, OnIterationComplete) {
  // Initialize the shared memory regions
  constexpr size_t N = 8;
  uint8_t inline_8bit_counters[N];
  uintptr_t pcs[N];

  // This needs a separate thread so the test can still drive the loop.
  sync_completion_t sync;
  std::thread t1([&inline_8bit_counters, &pcs, &sync]() {
    __sanitizer_cov_8bit_counters_init(inline_8bit_counters, inline_8bit_counters + N);
    __sanitizer_cov_pcs_init(pcs, pcs + N);
    sync_completion_signal(&sync);
  });

  // Keep prodding the test loop until the thread above is done making FIDL requests.
  zx_status_t status;
  while ((status = sync_completion_wait(&sync, ZX_MSEC(10))) == ZX_ERR_TIMED_OUT) {
    RunLoopUntilIdle();
  }
  ASSERT_EQ(status, ZX_OK);
  t1.join();

  // Add some data as if a fuzzing iteration were performed.
  for (uint8_t i = 0; i < N; ++i) {
    inline_8bit_counters[i] = i;
    pcs[i] = 0x1000 + i;
    __sanitizer_cov_trace_pc_indir(0x2000 + i);
  }
  coverage_.SendIterationComplete();

  // Shared memory regions should have been updated.
  SharedMemory i8bc_shmem;
  EXPECT_TRUE(coverage_.MapPending(&i8bc_shmem));
  EXPECT_EQ(i8bc_shmem.len(), sizeof(inline_8bit_counters));

  uint8_t *i8bc_actual = reinterpret_cast<uint8_t *>(i8bc_shmem.addr());
  for (size_t i = 0; i < N; ++i) {
    EXPECT_EQ(inline_8bit_counters[i], i8bc_actual[i]);
  }

  SharedMemory pcs_shmem;
  EXPECT_TRUE(coverage_.MapPending(&pcs_shmem));
  EXPECT_EQ(pcs_shmem.len(), sizeof(pcs));

  uintptr_t *pcs_actual = reinterpret_cast<uintptr_t *>(pcs_shmem.addr());
  for (size_t i = 0; i < N; ++i) {
    EXPECT_EQ(pcs[i], pcs_actual[i]);
  }

  // The proxy should have indicated completion.
  EXPECT_TRUE(coverage_.HasCompleted());
}

}  // namespace fuzzing
