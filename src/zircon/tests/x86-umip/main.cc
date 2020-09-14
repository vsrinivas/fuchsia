// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <assert.h>
#include <cpuid.h>
#include <lib/test-exceptions/exception-catcher.h>
#include <lib/test-exceptions/exception-handling.h>
#include <lib/zx/channel.h>
#include <lib/zx/process.h>
#include <lib/zx/thread.h>
#include <lib/zx/time.h>
#include <lib/zx/vmo.h>
#include <stdio.h>
#include <threads.h>
#include <zircon/compiler.h>
#include <zircon/syscalls.h>
#include <zircon/syscalls/exception.h>
#include <zircon/syscalls/port.h>
#include <zircon/threads.h>

#include <mutex>

#include <zxtest/zxtest.h>

enum class Instruction {
  SGDT,
  SIDT,
  SLDT,
  STR,
  SMSW,
  NOOP,          // Used to ensure harness does not always report failure
  MOV_NONCANON,  // Used to ensure harness does not always report success
};

namespace {

bool is_umip_supported() {
  uint32_t eax, ebx, ecx, edx;
  if (__get_cpuid_count(7, 0, &eax, &ebx, &ecx, &edx) != 1) {
    return false;
  }
  return ecx & (1u << 2);
}

// If this returns true, the instruction is expected to cause a #GP if it
// is executed.
bool isn_should_crash(Instruction isn) {
  switch (isn) {
    case Instruction::SGDT:
    case Instruction::SIDT:
    case Instruction::SLDT:
    case Instruction::STR:
    case Instruction::SMSW:
      // If UMIP is supported, the kernel should have turned it on.
      return is_umip_supported();
    case Instruction::NOOP:
      return false;
    case Instruction::MOV_NONCANON:
      return true;
  }
  __builtin_trap();
}

struct ThreadFuncArg {
  Instruction isn;
  std::mutex mutex;
};

int isn_thread_func(void* raw_arg) {
  auto arg = static_cast<ThreadFuncArg*>(raw_arg);
  // The thread that created this thread holds the lock when it spawns the
  // thread, so that execution is blocked initially.
  arg->mutex.lock();

  auto isn = arg->isn;

  alignas(16) uint8_t scratch_buf[16];

  switch (isn) {
    case Instruction::SGDT: {
      __asm__ volatile("sgdt %0" : "=m"(*scratch_buf));
      break;
    }
    case Instruction::SIDT: {
      __asm__ volatile("sidt %0" : "=m"(*scratch_buf));
      break;
    }
    case Instruction::SLDT: {
      __asm__ volatile("sldt %0" : "=m"(*scratch_buf));
      break;
    }
    case Instruction::STR: {
      __asm__ volatile("str %0" : "=m"(*scratch_buf));
      break;
    }
    case Instruction::SMSW: {
      uint64_t msw = 0;
      __asm__ volatile("smsw %0" : "=r"(msw) : : "memory");
      break;
    }
    case Instruction::NOOP: {
      __asm__ volatile("nop");
      break;
    }
    case Instruction::MOV_NONCANON: {
      // We use a non-canonical address in order to produce a #GP, which we
      // specifically want to test (as opposed to other fault types such as
      // page faults).
      uint8_t* v = reinterpret_cast<uint8_t*>(1ULL << 63);
      __asm__ volatile("movq $0, %0" : "=m"(*v));
      break;
    }
  }

  arg->mutex.unlock();
  return 0;
}

void test_instruction(Instruction isn) {
  ThreadFuncArg arg;
  arg.isn = isn;

  zx::thread thread;
  zx::channel exception_channel;
  test_exceptions::ExceptionCatcher catcher;
  {
    std::lock_guard<std::mutex> guard(arg.mutex);

    thrd_t thread_obj;
    ASSERT_EQ(thrd_create(&thread_obj, isn_thread_func, static_cast<void*>(&arg)), thrd_success);
    ASSERT_EQ(zx::unowned_thread(thrd_get_zx_handle(thread_obj))
                  ->duplicate(ZX_RIGHT_SAME_RIGHTS, &thread),
              ZX_OK);
    thrd_detach(thread_obj);

    ASSERT_EQ(catcher.Start(thread), ZX_OK);
    // Release the lock, so that the thread can run.
  }

  // Wait for crash or thread completion.
  zx::status<zx::exception> result = catcher.ExpectException();
  if (result.is_ok()) {
    zx_exception_report_t report = {};
    ASSERT_EQ(thread.get_info(ZX_INFO_THREAD_EXCEPTION_REPORT, &report, sizeof(report), NULL, NULL),
              ZX_OK);
    EXPECT_TRUE(isn_should_crash(isn));
    // These instructions should cause a GPF
    EXPECT_EQ(report.header.type, ZX_EXCP_GENERAL);
    // Exit the thread.
    test_exceptions::ExitExceptionCThread(std::move(result.value()));
  } else {
    ASSERT_EQ(result.status_value(), ZX_ERR_PEER_CLOSED);
    // Thread terminated normally so the instruction did not crash
    ASSERT_FALSE(isn_should_crash(isn));
  }
}

TEST(X86UmipTestCase, Sgdt) { ASSERT_NO_FAILURES(test_instruction(Instruction::SGDT)); }

TEST(X86UmipTestCase, Sidt) { ASSERT_NO_FAILURES(test_instruction(Instruction::SIDT)); }

TEST(X86UmipTestCase, Sldt) { ASSERT_NO_FAILURES(test_instruction(Instruction::SLDT)); }

TEST(X86UmipTestCase, Smsw) {
  bool should_skip = false;
  if (is_umip_supported()) {
    // If UMIP is supported, check if we're running under KVM.  On host
    // hardware that does not support UMIP, KVM misemulates UMIP's effect on
    // the SMSW instruction.
    uint32_t eax;
    uint32_t name[3];
    __cpuid(0x40000000, eax, name[0], name[1], name[2]);

    if (!memcmp(reinterpret_cast<const char*>(name), "KVMKVMKVM\0\0\0", sizeof(name))) {
      should_skip = true;
    }
  }

  if (!should_skip) {
    ASSERT_NO_FAILURES(test_instruction(Instruction::SMSW));
  }
}

TEST(X86UmipTestCase, Str) { ASSERT_NO_FAILURES(test_instruction(Instruction::STR)); }

TEST(X86UmipTestCase, Noop) { ASSERT_NO_FAILURES(test_instruction(Instruction::NOOP)); }

TEST(X86UmipTestCase, MoveNoncanonical) {
  ASSERT_NO_FAILURES(test_instruction(Instruction::MOV_NONCANON));
}

}  // namespace
