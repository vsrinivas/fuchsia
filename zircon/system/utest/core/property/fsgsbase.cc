// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifdef __x86_64__

// The ZX_PROP_REGISTER_FS and ZX_PROP_REGISTER_GS properties exist as
// stand-ins for the rdfsbase/wrfsbase and rdgsbase/rdgsbase instructions
// available on newer x86 processors.  So this test ensures that the system
// calls behave consistently with the machine instructions.

// This test exercises the CPU instructions as well as the system calls, so
// it can verify that they interact consistently.  To test both with and
// without the CPU instructions available, QEMU (with or without KVM) can
// be passed the `-cpu -fsgsbase` switch when running on hardware that does
// actually support it to emulate hardware that does not.

#include <cpuid.h>
#include <lib/arch/x86/cpuid.h>
#include <lib/elf-psabi/sp.h>
#include <lib/fit/defer.h>
#include <lib/zx/exception.h>
#include <lib/zx/thread.h>
#include <zircon/syscalls.h>
#include <zircon/syscalls/debug.h>
#include <zircon/syscalls/exception.h>
#include <zircon/syscalls/object.h>
#include <zircon/threads.h>

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string_view>
#include <thread>

#include <zxtest/zxtest.h>

namespace {

constexpr std::string_view kThreadName = "fsgsbase-test-thread";

// These values meet the "canonical address" test but are certain never to be
// valid user addresses.
constexpr uint64_t kInitialValue = 0xdead1;
constexpr uint64_t kWriteInsnValue = 0xdead2;

// This is a canonical address, but never a valid user address.
constexpr uint64_t kNonuserValue = -4096;

constexpr uint64_t kNoncanonicalValue = 0x1010101010101010;

constexpr uint64_t kUndefinedInsnValue = 0xfed1bad1;
constexpr uint64_t kDatumValue = 0x1234567890abcdef;

// When the {rd,wr}{fs,gs}base instructions are not available, this is the
// Zircon exception that will be generated for illegal instruction traps.
constexpr uint32_t kNoInsnsException = ZX_EXCP_UNDEFINED_INSTRUCTION;

// This is the Zircon exception that will be generated for general protection
// faults produced by use of noncanonical addresses.
constexpr uint32_t kNoncanonicalException = ZX_EXCP_GENERAL;

// Most of the test cases work by launching a separate thread that will attempt
// to use the %fs.base and %gs.base features directly.  Since %fs.base is used
// as the thread pointer in the normal Fuchsia compiler ABI, the code for this
// thread disables all compiler features that depend on it (e.g. SafeStack) and
// avoids standard runtime code that uses the full ABI.

#ifdef __clang__
#define BARE_FUNCTION [[clang::no_sanitize("all")]]
#else
#define BARE_FUNCTION
#endif

// When rdfsbase/rdgsbase are not available, there is no way for user code to
// retrieve the %fs.base and %gs.base values directly from the CPU.  Only
// memory accesses using them in the effective address calculation can be used.

BARE_FUNCTION uint64_t LoadFromFs() {
  uint64_t value;
  __asm__("mov %%fs:0, %0" : "=r"(value));
  return value;
}

BARE_FUNCTION uint64_t LoadFromGs() {
  uint64_t value;
  __asm__("mov %%gs:0, %0" : "=r"(value));
  return value;
}

// These are defined in assembly so we can know the instruction's exact PC when
// examining thread register state in the exception handler.  Each entry point
// is the instruction itself, and the *End symbol is just after it.  So the
// exception handler will move the PC ahead to skip the instruction after it
// faults, and then change the %rax register value to synthesize its result.
extern "C" uint64_t RdFsBase(), RdGsBase();
extern "C" void WrFsBase(uint64_t), WrGsBase(uint64_t);
extern "C" void RdFsBaseEnd(), RdGsBaseEnd(), WrFsBaseEnd(), WrGsBaseEnd();
__asm__(
    R"""(
    .pushsection .text.RdFsBase, "a", %progbits
    .type RdFsBase, %function
    RdFsBase:
      .cfi_startproc
      rdfsbase %rax
    RdFsBaseEnd:
      ret
      .cfi_endproc
    .size RdFsBase, . - RdFsBase
    .popsection

    .pushsection .text.RdGsBase, "a", %progbits
    .type RdGsBase, %function
    RdGsBase:
      .cfi_startproc
      rdgsbase %rax
    RdGsBaseEnd:
      ret
      .cfi_endproc
    .size RdGsBase, . - RdGsBase
    .popsection

    .pushsection .text.WrFsBase, "a", %progbits
    .type WrFsBase, %function
    WrFsBase:
      .cfi_startproc
      wrfsbase %rdi
    WrFsBaseEnd:
      ret
      .cfi_endproc
    .size WrFsBase, . - WrFsBase
    .popsection

    .pushsection .text.WrGsBase, "a", %progbits
    .type WrGsBase, %function
    WrGsBase:
      .cfi_startproc
      wrgsbase %rdi
    WrGsBaseEnd:
      ret
      .cfi_endproc
    .size WrGsBase, . - WrGsBase
    .popsection
    )""");

// The tests are exactly the same for the two registers, so everything is
// parameterized by the WhichRegister data structure.

struct WhichRegister {
  uint64_t zx_thread_state_general_regs_t::*member;
  uint32_t property;
  void (*write_insn)(uint64_t);
  void (*after_write_insn)();
  uint64_t (*read_insn)();
  void (*after_read_insn)();
  uint64_t (*load_from)();
};

constexpr WhichRegister kFs = {
    .member = &zx_thread_state_general_regs_t::fs_base,
    .property = ZX_PROP_REGISTER_FS,
    .write_insn = WrFsBase,
    .after_write_insn = WrFsBaseEnd,
    .read_insn = RdFsBase,
    .after_read_insn = RdFsBaseEnd,
    .load_from = LoadFromFs,
};

constexpr WhichRegister kGs = {
    .member = &zx_thread_state_general_regs_t::gs_base,
    .property = ZX_PROP_REGISTER_GS,
    .write_insn = WrGsBase,
    .after_write_insn = WrGsBaseEnd,
    .read_insn = RdGsBase,
    .after_read_insn = RdGsBaseEnd,
    .load_from = LoadFromGs,
};

// Some x86 CPUs support the instructions and some don't.  This test should
// work correctly in either case.
bool HaveInsns() {
  static bool have_insns = []() -> bool {
    arch::CpuidIo cpu;
    __cpuid_count(arch::CpuidExtendedFeatureFlagsB::kLeaf,
                  arch::CpuidExtendedFeatureFlagsB::kSubleaf, cpu.values_[arch::CpuidIo::kEax],
                  cpu.values_[arch::CpuidIo::kEbx], cpu.values_[arch::CpuidIo::kEcx],
                  cpu.values_[arch::CpuidIo::kEdx]);
    return arch::CpuidExtendedFeatureFlagsB::Get().ReadFrom(&cpu).fsgsbase();
  }();
  return have_insns;
}

// This is all the state communicated between the little test thread and the
// controlling test code.  The TestFsGsThread function just follows these
// instructions in a rote fashion.  The controlling test expects different
// results (values delivered and/or faults triggered) based on the values used
// and on whether the CPU supports the instructions.
struct TestData {
  uint64_t write_insn = -1;  // Write this value using the write_insn function.
  uint64_t write_prop = -1;  // Write this value using the system call.

  uint64_t read_insn = -1;  // Collect the value read using read_insn.
  uint64_t read_prop = -1;  // Collect the value read using the system call.

  uint64_t load = -1;  // Collect the value using the load_from function.

  zx_handle_t thread;  // Thread-self handle for use in the system call.

  // Results of the system call attempts.
  zx_status_t get_status = ZX_ERR_BAD_STATE;
  zx_status_t set_status = ZX_ERR_BAD_STATE;
};

// This runs in a raw thread with minimal stack and avoids anything that wants
// to use the thread register, since that's %fs.base.
BARE_FUNCTION [[noreturn]] void TestFsGsThread(const WhichRegister& reg, TestData& test) {
  // Read the old value via the instruction.
  test.read_insn = reg.read_insn();

  // Write the new value via the instruction.
  reg.write_insn(test.write_insn);

  // Read the value via the system call.
  test.get_status =
      zx_object_get_property(test.thread, reg.property, &test.read_prop, sizeof(test.read_prop));

  // Write the value via the system call.
  test.set_status =
      zx_object_set_property(test.thread, reg.property, &test.write_prop, sizeof(test.write_prop));

  // Load from the value as a pointer via the addressing prefix.
  test.load = reg.load_from();

  // Synchronize all stores with the waiting thread.
  std::atomic_thread_fence(std::memory_order_seq_cst);

  // All done.
  zx_thread_exit();
}

// A handy type for C++ new[] to deliver a correctly-aligned stack allocation.
struct alignas(16) StackAligned {
  std::byte bytes[16];
};

void TestInThread(const WhichRegister& reg, TestData& test, uint32_t expected_read_exception,
                  uint32_t expected_write_exception, uintptr_t expected_fault) {
  // Create a new raw thread and give it a small stack.

  std::unique_ptr<StackAligned[]> thread_stack(
      new StackAligned[zx_system_get_page_size() / sizeof(StackAligned)]);

  zx::thread thread;
  ASSERT_OK(
      zx::thread::create(*zx::process::self(), kThreadName.data(), kThreadName.size(), 0, &thread));

  // Store the thread's own handle so it can make the system calls.
  test.thread = thread.get();

  // Synchronize all stores before the new thread reads them.
  std::atomic_thread_fence(std::memory_order_seq_cst);

  // Set up to receive the new thread's exceptions.
  zx::channel exc_channel;
  ASSERT_OK(thread.create_exception_channel(0, &exc_channel));

  // Start the thread suspended so its register state can be modified.
  zx::suspend_token suspended;
  ASSERT_OK(thread.suspend(&suspended));

  // The thread will start with the call TestFsGsThread(reg, test).
  const uintptr_t sp = compute_initial_stack_pointer(
      reinterpret_cast<uintptr_t>(thread_stack.get()), zx_system_get_page_size());
  const uintptr_t pc = reinterpret_cast<uintptr_t>(&TestFsGsThread);
  const uintptr_t arg1 = reinterpret_cast<uintptr_t>(&reg);
  const uintptr_t arg2 = reinterpret_cast<uintptr_t>(&test);
  ASSERT_OK(thread.start(pc, sp, arg1, arg2));

  // The thread was "started suspended", but that means it starts up and then
  // suspends, so it has to be synchronized.
  zx_signals_t signals = ZX_THREAD_SUSPENDED;
  ASSERT_OK(thread.wait_one(signals, zx::time::infinite(), &signals));
  ASSERT_TRUE(signals & ZX_THREAD_SUSPENDED);

  // Now it's possible examine and mutate the initial register state.
  zx_thread_state_general_regs_t regs;
  ASSERT_OK(thread.read_state(ZX_THREAD_STATE_GENERAL_REGS, &regs, sizeof(regs)));
  EXPECT_EQ(pc, regs.rip);
  EXPECT_EQ(sp, regs.rsp);
  EXPECT_EQ(arg1, regs.rdi);
  EXPECT_EQ(arg2, regs.rsi);
  EXPECT_EQ(arg2, regs.rsi);

  // Set the register's initial value on thread start.
  regs.*reg.member = kInitialValue;
  ASSERT_OK(thread.write_state(ZX_THREAD_STATE_GENERAL_REGS, &regs, sizeof(regs)));

  // Now let the thread run.
  suspended.reset();

  // Wait for an exception message and/or thread death.
  zx_wait_item_t wait_items[] = {
      {.handle = exc_channel.get(), .waitfor = ZX_CHANNEL_READABLE},
      {.handle = thread.get(), .waitfor = ZX_THREAD_TERMINATED},
  };
  const zx_wait_item_t& wait_channel = wait_items[0];
  const zx_wait_item_t& wait_thread = wait_items[1];
  ASSERT_OK(zx::handle::wait_many(wait_items, std::size(wait_items), zx::time::infinite()));

  // If the rd*base instruction is expected to fault, catch that fault.
  if (expected_read_exception) {
    ASSERT_TRUE(wait_channel.pending & ZX_CHANNEL_READABLE);
    ASSERT_FALSE(wait_thread.pending & ZX_THREAD_TERMINATED);

    // Read the exception message.
    zx::exception exc;
    zx_exception_info_t exc_info;
    uint32_t nbytes = 0, nhandles = 0;
    ASSERT_OK(exc_channel.read(0, &exc_info, exc.reset_and_get_address(), sizeof(exc_info), 1,
                               &nbytes, &nhandles));
    ASSERT_EQ(sizeof(exc_info), nbytes);
    ASSERT_EQ(1u, nhandles);

    // Verify it was the expected fault at the expected PC.
    EXPECT_EQ(expected_read_exception, exc_info.type);
    zx_thread_state_general_regs_t regs;
    ASSERT_OK(thread.read_state(ZX_THREAD_STATE_GENERAL_REGS, &regs, sizeof(regs)));
    EXPECT_EQ(reinterpret_cast<uintptr_t>(reg.read_insn), regs.rip);

    // Warp the PC past the instruction and set the return-value register.
    regs.rip = reinterpret_cast<uintptr_t>(reg.after_read_insn);
    regs.rax = kUndefinedInsnValue;
    ASSERT_OK(thread.write_state(ZX_THREAD_STATE_GENERAL_REGS, &regs, sizeof(regs)));

    // Let the thread resume from the exception at the new PC.
    constexpr uint32_t kExceptionState = ZX_EXCEPTION_STATE_HANDLED;
    ASSERT_OK(exc.set_property(ZX_PROP_EXCEPTION_STATE, &kExceptionState, sizeof(kExceptionState)));
    exc.reset();

    // Wait for the next fault or completion.
    ASSERT_OK(zx::handle::wait_many(wait_items, std::size(wait_items), zx::time::infinite()));
  }

  // If the wr*base instruction is expected to fault, catch that fault.
  if (expected_write_exception) {
    ASSERT_TRUE(wait_channel.pending & ZX_CHANNEL_READABLE);
    ASSERT_FALSE(wait_thread.pending & ZX_THREAD_TERMINATED);

    // Read the exception message.
    zx::exception exc;
    zx_exception_info_t exc_info;
    uint32_t nbytes = 0, nhandles = 0;
    ASSERT_OK(exc_channel.read(0, &exc_info, exc.reset_and_get_address(), sizeof(exc_info), 1,
                               &nbytes, &nhandles));
    ASSERT_EQ(sizeof(exc_info), nbytes);
    ASSERT_EQ(1u, nhandles);

    // Verify it was the expected fault at the expected PC.
    EXPECT_EQ(expected_write_exception, exc_info.type);
    zx_thread_state_general_regs_t regs;
    ASSERT_OK(thread.read_state(ZX_THREAD_STATE_GENERAL_REGS, &regs, sizeof(regs)));
    EXPECT_EQ(reinterpret_cast<uintptr_t>(reg.write_insn), regs.rip);

    // Warp the PC past the instruction so the thread can continue.  The
    // write_insn function returns void, so regs.rax doesn't matter here.
    regs.rip = reinterpret_cast<uintptr_t>(reg.after_write_insn);
    ASSERT_OK(thread.write_state(ZX_THREAD_STATE_GENERAL_REGS, &regs, sizeof(regs)));

    // Let the thread resume from the exception at the new PC.
    constexpr uint32_t kExceptionState = ZX_EXCEPTION_STATE_HANDLED;
    ASSERT_OK(exc.set_property(ZX_PROP_EXCEPTION_STATE, &kExceptionState, sizeof(kExceptionState)));
    exc.reset();

    // Wait for the next fault or completion.
    ASSERT_OK(zx::handle::wait_many(wait_items, std::size(wait_items), zx::time::infinite()));
  }

  // If the load was expected to fault on a bad address, catch that fault.
  if (expected_fault) {
    ASSERT_TRUE(wait_channel.pending & ZX_CHANNEL_READABLE);
    ASSERT_FALSE(wait_thread.pending & ZX_THREAD_TERMINATED);

    // Read the exception message.
    zx::exception exc;
    zx_exception_info_t exc_info;
    uint32_t nbytes = 0, nhandles = 0;
    ASSERT_OK(exc_channel.read(0, &exc_info, exc.reset_and_get_address(), sizeof(exc_info), 1,
                               &nbytes, &nhandles));
    ASSERT_EQ(sizeof(exc_info), nbytes);
    ASSERT_EQ(1u, nhandles);

    // Verify it was the expected fault at the expected fault address.  We
    // don't bother to check for precise PC here, since we don't really
    // need to mutate the register state and resume as in the cases above.
    EXPECT_EQ(ZX_EXCP_FATAL_PAGE_FAULT, exc_info.type);
    zx_exception_report_t report;
    ASSERT_OK(thread.get_info(ZX_INFO_THREAD_EXCEPTION_REPORT, &report, sizeof(report), nullptr,
                              nullptr));

    EXPECT_EQ(ZX_EXCP_FATAL_PAGE_FAULT, report.header.type);
    EXPECT_EQ(expected_fault, report.context.arch.u.x86_64.cr2);

    // Force the thread to exit when it resumes.
    constexpr uint32_t kExceptionState = ZX_EXCEPTION_STATE_THREAD_EXIT;
    ASSERT_OK(exc.set_property(ZX_PROP_EXCEPTION_STATE, &kExceptionState, sizeof(kExceptionState)));

    // Let the thread resume and exit.
    exc.reset();

    // Wait for termination.
    ASSERT_OK(zx::handle::wait_many(wait_items, std::size(wait_items), zx::time::infinite()));
  }

  // All expected faults (if any) should have been handled above.
  // So the thread should have terminated.
  EXPECT_TRUE(wait_thread.pending & ZX_THREAD_TERMINATED);
  EXPECT_FALSE(wait_channel.pending & ZX_CHANNEL_READABLE);
}

// The valid case has no faults unless the instructions are not supported.
void TestValid(const WhichRegister& reg) {
  const bool have_insns = HaveInsns();
  const uint32_t insn_except = have_insns ? 0 : kNoInsnsException;
  uint64_t datum = kDatumValue;
  const uintptr_t datum_address = reinterpret_cast<uintptr_t>(&datum);
  TestData test = {
      .write_insn = kWriteInsnValue,
      .write_prop = datum_address,
  };
  ASSERT_NO_FATAL_FAILURE(TestInThread(reg, test, insn_except, insn_except, 0));
  EXPECT_OK(test.get_status);
  EXPECT_OK(test.set_status);
  if (have_insns) {
    EXPECT_EQ(kInitialValue, test.read_insn);
    EXPECT_EQ(kWriteInsnValue, test.read_prop);
  } else {
    EXPECT_EQ(kUndefinedInsnValue, test.read_insn);
    EXPECT_EQ(kInitialValue, test.read_prop);
  }
  EXPECT_EQ(datum, test.load);
}

TEST(Property, RegisterFs) { ASSERT_NO_FATAL_FAILURE(TestValid(kFs)); }

TEST(Property, RegisterGs) { ASSERT_NO_FATAL_FAILURE(TestValid(kGs)); }

// This case is just as valid but is using a value that's a bad address.
// So the only thing that should be different is the final load, that
// faults with the expected address instead of succeeding.
void TestFault(const WhichRegister& reg) {
  // Allocate a VMAR to get some known-valid user address space that is
  // sure to be inaccessible.
  zx::vmar vmar;
  uintptr_t faulting_address;
  ASSERT_OK(
      zx::vmar::root_self()->allocate(0, 0, zx_system_get_page_size(), &vmar, &faulting_address));
  auto cleanup_vmar = fit::defer([&vmar]() { vmar.destroy(); });

  const bool have_insns = HaveInsns();
  const uint32_t insn_except = have_insns ? 0 : kNoInsnsException;
  TestData test = {
      .write_insn = kWriteInsnValue,
      .write_prop = faulting_address,
  };
  ASSERT_NO_FATAL_FAILURE(TestInThread(reg, test, insn_except, insn_except, faulting_address));
  EXPECT_OK(test.get_status);
  EXPECT_OK(test.set_status);
  if (have_insns) {
    EXPECT_EQ(kInitialValue, test.read_insn);
    EXPECT_EQ(kWriteInsnValue, test.read_prop);
  } else {
    EXPECT_EQ(kUndefinedInsnValue, test.read_insn);
    EXPECT_EQ(kInitialValue, test.read_prop);
  }
}

TEST(Property, RegisterFsFault) { ASSERT_NO_FATAL_FAILURE(TestFault(kFs)); }

TEST(Property, RegisterGsFault) { ASSERT_NO_FATAL_FAILURE(TestFault(kGs)); }

// Both machine instructions and system calls refuse noncanonical values.
void TestNoncanonical(const WhichRegister& reg) {
  const bool have_insns = HaveInsns();
  const uint32_t read_exception = have_insns ? 0 : kNoInsnsException;
  const uint32_t write_exception = have_insns ? kNoncanonicalException : kNoInsnsException;
  TestData test = {
      .write_insn = kNoncanonicalValue,
      .write_prop = kNoncanonicalValue,
  };

  ASSERT_NO_FATAL_FAILURE(TestInThread(reg, test, read_exception, write_exception, kInitialValue));
  if (have_insns) {
    EXPECT_EQ(kInitialValue, test.read_insn);
  } else {
    EXPECT_EQ(kUndefinedInsnValue, test.read_insn);
  }

  // Since writing wasn't allowed, reading should still find the initial value.
  EXPECT_OK(test.get_status);
  EXPECT_EQ(kInitialValue, test.read_prop);

  // Writing via system call should fail just like the instruction faults.
  EXPECT_EQ(ZX_ERR_INVALID_ARGS, test.set_status);
}

TEST(Property, RegisterFsNoncanonical) { ASSERT_NO_FATAL_FAILURE(TestNoncanonical(kFs)); }

TEST(Property, RegisterGsNoncanonical) { ASSERT_NO_FATAL_FAILURE(TestNoncanonical(kGs)); }

// Non-user addresses are allowed even though they'll always fault when used.
void TestNonuser(const WhichRegister& reg) {
  const bool have_insns = HaveInsns();
  const uint32_t read_exception = have_insns ? 0 : kNoInsnsException;
  const uint32_t write_exception = have_insns ? 0 : kNoInsnsException;
  TestData test = {
      .write_insn = kNonuserValue,
      .write_prop = kNonuserValue,
  };

  ASSERT_NO_FATAL_FAILURE(TestInThread(reg, test, read_exception, write_exception, kNonuserValue));

  EXPECT_OK(test.set_status);
  EXPECT_OK(test.get_status);

  if (have_insns) {
    EXPECT_EQ(kInitialValue, test.read_insn);
    EXPECT_EQ(kNonuserValue, test.read_prop);
  } else {
    EXPECT_EQ(kUndefinedInsnValue, test.read_insn);
    EXPECT_EQ(kInitialValue, test.read_prop);
  }
}

TEST(Property, RegisterFsNonuser) { ASSERT_NO_FATAL_FAILURE(TestNonuser(kFs)); }

TEST(Property, RegisterGsNonuser) { ASSERT_NO_FATAL_FAILURE(TestNonuser(kGs)); }

// The invalid uses of the system calls are easy to test without the
// separate test thread, since no register values will actually change.

// The system calls only work with a thread handle.
void TestNonThread(const WhichRegister& reg) {
  uint64_t x = 0xfeedfacedeadbeef;
  EXPECT_EQ(ZX_ERR_WRONG_TYPE, zx::process::self()->get_property(reg.property, &x, sizeof(x)));
  EXPECT_EQ(ZX_ERR_WRONG_TYPE, zx::process::self()->set_property(reg.property, &x, sizeof(x)));
}

TEST(Property, RegisterFsNonThread) { ASSERT_NO_FATAL_FAILURE(TestNonThread(kFs)); }

TEST(Property, RegisterGsNonThread) { ASSERT_NO_FATAL_FAILURE(TestNonThread(kGs)); }

// The system calls only work with a handle to the calling thread.
void TestOtherThread(const WhichRegister& reg) {
  // Start another thread that will block on the lock until we release it.
  std::mutex lock;
  std::unique_lock main_locked(lock);
  std::thread other([&lock]() { std::lock_guard other_locked(lock); });
  auto cleanup_thread = fit::defer([main_locked = std::move(main_locked), &other]() mutable {
    main_locked.unlock();
    other.join();
  });

  // That thread is alive, so its handle is valid.
  zx::unowned_thread thread_handle{native_thread_get_zx_handle(other.native_handle())};

  uint64_t x = 0xfeedfacedeadbeef;
  EXPECT_EQ(ZX_ERR_ACCESS_DENIED, thread_handle->get_property(reg.property, &x, sizeof(x)));
  EXPECT_EQ(ZX_ERR_ACCESS_DENIED, thread_handle->set_property(reg.property, &x, sizeof(x)));
}

TEST(Property, RegisterFsOtherThread) { ASSERT_NO_FATAL_FAILURE(TestOtherThread(kFs)); }

TEST(Property, RegisterGsOtherThread) { ASSERT_NO_FATAL_FAILURE(TestOtherThread(kGs)); }

void TestTooSmall(const WhichRegister& reg) {
  uint32_t x = 0xdeadbeef;
  EXPECT_EQ(ZX_ERR_BUFFER_TOO_SMALL, zx::thread::self()->get_property(reg.property, &x, sizeof(x)));
  EXPECT_EQ(ZX_ERR_BUFFER_TOO_SMALL, zx::thread::self()->set_property(reg.property, &x, sizeof(x)));
}

TEST(Property, RegisterFsTooSmall) { ASSERT_NO_FATAL_FAILURE(TestTooSmall(kFs)); }

TEST(Property, RegisterGsTooSmall) { ASSERT_NO_FATAL_FAILURE(TestTooSmall(kGs)); }

}  // namespace

#endif  // __x86_64__
