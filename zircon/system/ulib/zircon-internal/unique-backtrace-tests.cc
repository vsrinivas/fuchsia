// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/zircon-internal/unique-backtrace.h>

#include <cstdint>

#include <zxtest/zxtest.h>

#ifdef __Fuchsia__
#include <lib/elf-psabi/sp.h>
#include <lib/zircon-internal/default_stack_size.h>
#include <lib/zx/channel.h>
#include <lib/zx/exception.h>
#include <lib/zx/handle.h>
#include <lib/zx/process.h>
#include <lib/zx/thread.h>
#include <zircon/syscalls/debug.h>
#include <zircon/syscalls/exception.h>

#include <memory>
#include <string_view>
#endif  // __Fuchsia__

namespace {

// The BUILD.gn rule defines ICF_WORKS to false when using various kinds of
// compiler instrumentation.  Some of these cause identical functions in the
// source to become nonidentical code.  So the baseline verification that ICF
// happens when expected can't be relied on in these builds.
constexpr bool kIcfExpected = ICF_WORKS;

// Since it's never inlined, this will always get the actual return address.
// However, it's up to its caller to make sure that the return address is the
// PC in the actual caller rather than the caller's caller (or on up) via
// either inlining or tail-call optimization.
[[gnu::noinline]] uintptr_t RecordCaller() {
  return reinterpret_cast<uintptr_t>(__builtin_return_address(0));
}

// Even if inlined away, this prevents the compiler from thinking it knows the
// value.  Thus it can't do tail-call optimization for the function call that
// yields the argument value.
uintptr_t Launder(uintptr_t value) {
  // Clang doesn't really understand GCC constraints that give it multiple
  // options to choose from, so it generates lousy code for "=g" or even "=rm".
  __asm__("" : "=r"(value) : "0"(value));
  return value;
}

// Hence these four calls will get their own true PC address and never their
// caller's address by dint of a tail-call-optimized jump to RecordCaller().
// Since they can't be inlined, they should always be appropriate candidates
// for ICF (or the LTO equivalent).  The first pair are fully identical (unless
// modified by compiler instrumentation; see kIcfExpected, above), so they
// should be folded at link time into returning the same PC value at runtime.
// The second pair use the API under test to prevent that happening, so they
// should always have distinct PC values to return at runtime.

[[gnu::noinline]] uintptr_t IcfExpected1() { return Launder(RecordCaller()); }

[[gnu::noinline]] uintptr_t IcfExpected2() { return Launder(RecordCaller()); }

[[gnu::noinline]] uintptr_t IcfPrevented1() {
  ENSURE_UNIQUE_BACKTRACE();
  return Launder(RecordCaller());
}

[[gnu::noinline]] uintptr_t IcfPrevented2() {
  ENSURE_UNIQUE_BACKTRACE();
  return Launder(RecordCaller());
}

TEST(ZirconInternalUniqueBacktraceTests, IcfExpected) {
  uintptr_t caller1 = IcfExpected1();
  uintptr_t caller2 = IcfExpected2();
  if (kIcfExpected) {
    EXPECT_EQ(caller1, caller2);
  }
}

TEST(ZirconInternalUniqueBacktraceTests, IcfPrevented) {
  uintptr_t caller1 = IcfPrevented1();
  uintptr_t caller2 = IcfPrevented2();
  EXPECT_NE(caller1, caller2);
}

// ICF also works for indirect duplication: once two callees have been folded
// together, the callers become identical enough to be folded together
// themselves.  This can be prevented in either (or both) of two ways:
//
//  * Prevent the indirect deduplication "indirectly" by preventing the
//    deduplication of the callees.  Two callers with identical code but
//    different relocations (call targets) cannot be folded together.
//
//  * Prevent the indirect deduplication "directly" by preventing the
//    deduplication of the callers.  Even if the callees of the two callers get
//    folded together, the callers themselves won't be.

[[gnu::noinline]] uintptr_t IndirectIcfExpected1() { return Launder(IcfExpected1()); }

[[gnu::noinline]] uintptr_t IndirectIcfExpected2() { return Launder(IcfExpected2()); }

[[gnu::noinline]] uintptr_t IndirectIcfPreventedIndirectly1() { return Launder(IcfPrevented1()); }

[[gnu::noinline]] uintptr_t IndirectIcfPreventedIndirectly2() { return Launder(IcfPrevented2()); }

[[gnu::noinline]] uintptr_t IndirectIcfPreventedDirectly1() {
  ENSURE_UNIQUE_BACKTRACE();
  IcfExpected1();
  return Launder(RecordCaller());
}

[[gnu::noinline]] uintptr_t IndirectIcfPreventedDirectly2() {
  ENSURE_UNIQUE_BACKTRACE();
  IcfExpected2();
  return Launder(RecordCaller());
}

TEST(ZirconInternalUniqueBacktraceTests, IndirectIcfExpected) {
  uintptr_t caller1 = IndirectIcfExpected1();
  uintptr_t caller2 = IndirectIcfExpected2();
  if (kIcfExpected) {
    EXPECT_EQ(caller1, caller2);
  }
}

TEST(ZirconInternalUniqueBacktraceTests, IndirectIcfPreventedIndirectly) {
  uintptr_t caller1 = IndirectIcfPreventedIndirectly1();
  uintptr_t caller2 = IndirectIcfPreventedIndirectly2();
  EXPECT_NE(caller1, caller2);
}

TEST(ZirconInternalUniqueBacktraceTests, IndirectIcfPreventedDirectly) {
  uintptr_t caller1 = IndirectIcfPreventedDirectly1();
  uintptr_t caller2 = IndirectIcfPreventedDirectly2();
  EXPECT_NE(caller1, caller2);
}

#ifdef __Fuchsia__

// kPcRegister is the PC member in the thread register state.
// kTrapExceptionType is the type of exception that __builtin_trap() causes.

#if defined(__aarch64__)

constexpr auto kPcRegister = &zx_thread_state_general_regs_t::pc;
constexpr zx_excp_type_t kTrapExceptionType = ZX_EXCP_SW_BREAKPOINT;

#elif defined(__x86_64__)

constexpr auto kPcRegister = &zx_thread_state_general_regs_t::rip;
constexpr zx_excp_type_t kTrapExceptionType = ZX_EXCP_UNDEFINED_INSTRUCTION;

#else
#error "what machine?"
#endif

// To test the crashing cases, we'll spawn a raw Zircon thread with no C
// library assistance so there are no hidden data structures to clean up after
// the thread is killed.

void CatchCrash(void (*crash_function)(), uintptr_t& crash_pc) {
  constexpr size_t kCrashThreadStackSize = ZIRCON_DEFAULT_STACK_SIZE;
  constexpr std::string_view kCrashThreadName = "zircon-internal crash test";

  zx::thread crash_thread;
  ASSERT_OK(zx::thread::create(*zx::process::self(), kCrashThreadName.data(),
                               kCrashThreadName.size(), 0, &crash_thread));

  // Set up to receive thread exceptions for the new thread.
  zx::channel exception_channel;
  ASSERT_OK(crash_thread.create_exception_channel(0, &exception_channel));

  // Start it running with a stack and PC at the crash function's entry point.
  std::unique_ptr<std::byte[]> crash_thread_stack =
      std::make_unique<std::byte[]>(kCrashThreadStackSize);
  const uintptr_t pc = reinterpret_cast<uintptr_t>(crash_function);
  const uintptr_t sp = compute_initial_stack_pointer(
      reinterpret_cast<uintptr_t>(crash_thread_stack.get()), kCrashThreadStackSize);
  ASSERT_OK(crash_thread.start(pc, sp, 0, 0));

  // Wait for the exception message and/or thread death.
  zx_wait_item_t wait_items[] = {
      {.handle = exception_channel.get(), .waitfor = ZX_CHANNEL_READABLE},
      {.handle = crash_thread.get(), .waitfor = ZX_THREAD_TERMINATED},
  };
  const zx_wait_item_t& wait_channel = wait_items[0];
  const zx_wait_item_t& wait_thread = wait_items[1];
  ASSERT_OK(zx::handle::wait_many(wait_items, std::size(wait_items), zx::time::infinite()));

  // The exception should happen first while the thread is still alive.
  ASSERT_TRUE(wait_channel.pending & ZX_CHANNEL_READABLE);
  ASSERT_FALSE(wait_thread.pending & ZX_THREAD_TERMINATED);

  // Read the exception message.
  zx::exception exc;
  zx_exception_info_t exc_info;
  uint32_t nbytes = 0, nhandles = 0;
  ASSERT_OK(exception_channel.read(0, &exc_info, exc.reset_and_get_address(), sizeof(exc_info), 1,
                                   &nbytes, &nhandles));
  ASSERT_EQ(sizeof(exc_info), nbytes);
  ASSERT_EQ(1u, nhandles);

  // When the exception handle is closed (by the zx::exception destructor at
  // the end of the function), the thread will resume from the exception.  Set
  // it up to "resume" by doing an immediate thread exit.  This should make it
  // safe to assume its stack will never be used again from here on out.  (The
  // stack will also be freed by a destructor at the end of the function.)
  constexpr uint32_t kExceptionState = ZX_EXCEPTION_STATE_THREAD_EXIT;
  ASSERT_OK(exc.set_property(ZX_PROP_EXCEPTION_STATE, &kExceptionState, sizeof(kExceptionState)));

  // Check it was the exception we expect for __builtin_trap().
  ASSERT_EQ(kTrapExceptionType, exc_info.type);

  // Now fetch the thread's register state when it hit the __builtin_trap().
  zx_thread_state_general_regs_t regs = {};
  ASSERT_OK(crash_thread.read_state(ZX_THREAD_STATE_GENERAL_REGS, &regs, sizeof(regs)));

  // Extract the PC of the crash site.
  crash_pc = regs.*kPcRegister;
  ASSERT_NE(0, crash_pc);
}

// The crashing entry points can't use anything but the basic stack.

#ifdef __clang__
#define BASIC_ABI [[clang::no_sanitize("all")]]
#else
#define BASIC_ABI  // We don't use anything in GCC that needs to be avoided.
#endif

BASIC_ABI [[noreturn]] void CrashWithIcfExpected1() { __builtin_trap(); }

BASIC_ABI [[noreturn]] void CrashWithIcfExpected2() { __builtin_trap(); }

BASIC_ABI [[noreturn]] void CrashWithIcfPrevented1() { CRASH_WITH_UNIQUE_BACKTRACE(); }

BASIC_ABI [[noreturn]] void CrashWithIcfPrevented2() { CRASH_WITH_UNIQUE_BACKTRACE(); }

TEST(ZirconInternalUniqueBacktraceTests, CrashWithIcfExpected) {
  uintptr_t crash1 = 0, crash2 = 0;
  ASSERT_NO_FATAL_FAILURES(CatchCrash(CrashWithIcfExpected1, crash1));
  ASSERT_NO_FATAL_FAILURES(CatchCrash(CrashWithIcfExpected2, crash2));
  if (kIcfExpected) {
    EXPECT_EQ(crash1, crash2);
  }
}

TEST(ZirconInternalUniqueBacktraceTests, CrashWithIcfPrevented) {
  uintptr_t crash1 = 0, crash2 = 0;
  ASSERT_NO_FATAL_FAILURES(CatchCrash(CrashWithIcfPrevented1, crash1));
  ASSERT_NO_FATAL_FAILURES(CatchCrash(CrashWithIcfPrevented2, crash2));
  EXPECT_NE(crash1, crash2);
}

BASIC_ABI [[gnu::noinline]] uintptr_t BasicAbiRecordCaller() {
  return reinterpret_cast<uintptr_t>(__builtin_return_address(0));
}

BASIC_ABI uintptr_t BasicAbiLaunder(uintptr_t value) {
  __asm__("" : "=r"(value) : "0"(value));
  return value;
}

BASIC_ABI [[gnu::noinline]] uintptr_t BasicAbiIcfExpected1() {
  return BasicAbiLaunder(BasicAbiRecordCaller());
}

BASIC_ABI [[gnu::noinline]] uintptr_t BasicAbiIcfExpected2() {
  return BasicAbiLaunder(BasicAbiRecordCaller());
}

BASIC_ABI [[gnu::noinline]] uintptr_t BasicAbiIndirectIcfExpected1() {
  return BasicAbiLaunder(BasicAbiIcfExpected1());
}

BASIC_ABI [[gnu::noinline]] uintptr_t BasicAbiIndirectIcfExpected2() {
  return BasicAbiLaunder(BasicAbiIcfExpected2());
}

BASIC_ABI [[noreturn]] void CrashWithIndirectIcfExpected1() {
  BasicAbiIndirectIcfExpected1();
  __builtin_trap();
}

BASIC_ABI [[noreturn]] void CrashWithIndirectIcfExpected2() {
  BasicAbiIndirectIcfExpected2();
  __builtin_trap();
}

BASIC_ABI [[noreturn]] void CrashWithIndirectIcfPreventedIndirectly1() { CrashWithIcfPrevented1(); }

BASIC_ABI [[noreturn]] void CrashWithIndirectIcfPreventedIndirectly2() { CrashWithIcfPrevented2(); }

BASIC_ABI [[noreturn]] void CrashWithIndirectIcfPreventedDirectly1() {
  BasicAbiIndirectIcfExpected1();
  CRASH_WITH_UNIQUE_BACKTRACE();
}

BASIC_ABI [[noreturn]] void CrashWithIndirectIcfPreventedDirectly2() {
  BasicAbiIndirectIcfExpected2();
  CRASH_WITH_UNIQUE_BACKTRACE();
}

TEST(ZirconInternalUniqueBacktraceTests, BasicAbiIcfExpected) {
  uintptr_t caller1 = BasicAbiIcfExpected1();
  uintptr_t caller2 = BasicAbiIcfExpected2();
  if (kIcfExpected) {
    EXPECT_EQ(caller1, caller2);
  }
}

TEST(ZirconInternalUniqueBacktraceTests, BasicAbiIndirectIcfExpected) {
  uintptr_t caller1 = BasicAbiIndirectIcfExpected1();
  uintptr_t caller2 = BasicAbiIndirectIcfExpected2();
  if (kIcfExpected) {
    EXPECT_EQ(caller1, caller2);
  }
}

TEST(ZirconInternalUniqueBacktraceTests, CrashWithIndirectIcfExpected) {
  uintptr_t crash1 = 0, crash2 = 0;
  ASSERT_NO_FATAL_FAILURES(CatchCrash(CrashWithIndirectIcfExpected1, crash1));
  ASSERT_NO_FATAL_FAILURES(CatchCrash(CrashWithIndirectIcfExpected2, crash2));
  if (kIcfExpected) {
    EXPECT_EQ(crash1, crash2);
  }
}

TEST(ZirconInternalUniqueBacktraceTests, CrashWithIndirectIcfPreventedDirectly) {
  uintptr_t crash1 = 0, crash2 = 0;
  ASSERT_NO_FATAL_FAILURES(CatchCrash(CrashWithIndirectIcfPreventedDirectly1, crash1));
  ASSERT_NO_FATAL_FAILURES(CatchCrash(CrashWithIndirectIcfPreventedDirectly2, crash2));
  EXPECT_NE(crash1, crash2);
}

TEST(ZirconInternalUniqueBacktraceTests, CrashWithIndirectIcfPreventedIndirectly) {
  uintptr_t crash1 = 0, crash2 = 0;
  ASSERT_NO_FATAL_FAILURES(CatchCrash(CrashWithIndirectIcfPreventedIndirectly1, crash1));
  ASSERT_NO_FATAL_FAILURES(CatchCrash(CrashWithIndirectIcfPreventedIndirectly2, crash2));
  EXPECT_NE(crash1, crash2);
}

#endif  // __Fuchsia__

}  // namespace
