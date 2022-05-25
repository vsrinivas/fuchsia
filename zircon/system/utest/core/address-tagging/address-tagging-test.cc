// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/arch/arm64/system.h>
#include <lib/elf-psabi/sp.h>
#include <lib/fit/defer.h>
#include <lib/zircon-internal/default_stack_size.h>
#include <lib/zx/exception.h>
#include <lib/zx/process.h>
#include <lib/zx/thread.h>
#include <zircon/features.h>
#include <zircon/hw/debug/arm64.h>
#include <zircon/syscalls.h>
#include <zircon/threads.h>

#include <test-utils/test-utils.h>
#include <zxtest/zxtest.h>

namespace {

#if defined(__aarch64__)

constexpr size_t kTagShift = 56;
constexpr uint8_t kTestTag = 0xAB;

constexpr uint64_t AddTag(uintptr_t ptr, uint8_t tag) {
  constexpr uint64_t kTagMask = UINT64_C(0xff) << kTagShift;
  ZX_ASSERT((kTagMask & ptr) == 0 && "Expected an untagged pointer.");
  return (static_cast<uint64_t>(tag) << kTagShift) | static_cast<uint64_t>(ptr);
}

template <typename T>
T* AddTag(T* ptr, uint8_t tag) {
  return reinterpret_cast<T*>(AddTag(reinterpret_cast<uintptr_t>(ptr), tag));
}

// Disable sanitizers for this because any sanitizer that involves doing a
// right shift to get a shadow memory location could cause a tag to leak into
// bit 55, leading to an incorrect shadow being referenced. This will affect
// ASan and eventually HWASan.
#ifdef __clang__
[[clang::no_sanitize("all")]]
#endif
void DerefTaggedPtr(int* ptr) {
  *ptr = 1;
}

TEST(TopByteIgnoreTests, AddressTaggingGetSystemFeaturesAArch64) {
  uint32_t features = 0;
  ASSERT_OK(zx_system_get_features(ZX_FEATURE_KIND_ADDRESS_TAGGING, &features));
  ASSERT_EQ(features, ZX_ARM64_FEATURE_ADDRESS_TAGGING_TBI);

  // Since TBI is supported, we can access tagged pointers.
  int val = 0;
  DerefTaggedPtr(AddTag(&val, kTestTag));
  ASSERT_EQ(val, 1);
}

using crash_function_t = void (*)(uintptr_t arg1, uintptr_t arg2);

// To test the crashing cases, we'll spawn a raw Zircon thread with no C
// library assistance so there are no hidden data structures to clean up after
// the thread is killed.
void CatchCrash(crash_function_t crash_function, uintptr_t arg1,
                fit::function<void(zx::thread&)> before_start, zx_exception_report_t* report) {
  zx::thread crash_thread;
  constexpr std::string_view kThreadName = "Address tagging test thread";
  ASSERT_OK(zx::thread::create(*zx::process::self(), kThreadName.data(), kThreadName.size(), 0,
                               &crash_thread));

  zx::suspend_token suspend;
  if (before_start) {
    // This ensures the thread will be suspended after starting. This is needed
    // for writing the thread state after it's running, but before we run anything
    // in the entry point.
    ASSERT_OK(crash_thread.suspend(&suspend));
    ASSERT_TRUE(suspend);
  }

  // Set up to receive thread exceptions for the new thread.
  zx::channel exception_channel;
  ASSERT_OK(crash_thread.create_exception_channel(0, &exception_channel));

  // Start it running with a stack and PC at the crash function's entry point.
  constexpr size_t kThreadStackSize = ZIRCON_DEFAULT_STACK_SIZE;
  std::unique_ptr<std::byte[]> crash_thread_stack = std::make_unique<std::byte[]>(kThreadStackSize);
  const uintptr_t pc = reinterpret_cast<uintptr_t>(crash_function);
  const uintptr_t sp = compute_initial_stack_pointer(
      reinterpret_cast<uintptr_t>(crash_thread_stack.get()), kThreadStackSize);
  ASSERT_OK(crash_thread.start(pc, sp, arg1, 0));

  if (before_start) {
    // The thread is now running, but it should be immediately suspended.
    zx_signals_t observed;
    ASSERT_OK(crash_thread.wait_one(ZX_THREAD_SUSPENDED, zx::time::infinite(), &observed));
    ASSERT_NE(observed & ZX_THREAD_SUSPENDED, 0);

    // Run any setup while the thread is suspended but before we dive into
    // the function.
    before_start(crash_thread);

    // Resume the thread.
    suspend.reset();
    ASSERT_EQ(crash_thread.wait_one(ZX_THREAD_RUNNING, zx::time::infinite(), &observed), ZX_OK);
  }

  // Wait for the exception channel to be readable. This will happen when
  // thread crashes and triggers the exception.
  tu_channel_wait_readable(exception_channel.get());

  // Get the FAR from the exception report.
  ASSERT_OK(crash_thread.get_info(ZX_INFO_THREAD_EXCEPTION_REPORT, report, sizeof(*report), nullptr,
                                  nullptr));

  // Read the exception message.
  zx::exception exc;
  zx_exception_info_t exc_info;
  uint32_t nbytes, nhandles;
  ASSERT_OK(exception_channel.read(0, &exc_info, exc.reset_and_get_address(), sizeof(exc_info), 1,
                                   &nbytes, &nhandles));
  ASSERT_EQ(sizeof(exc_info), nbytes);
  ASSERT_EQ(1, nhandles);

  // We can also retrieve the FAR from the thread debug regs. Let's just make
  // sure it's the same as what's in the exception report.
  zx_thread_state_debug_regs_t regs = {};
  ASSERT_OK(crash_thread.read_state(ZX_THREAD_STATE_DEBUG_REGS, &regs, sizeof(regs)));
  ASSERT_EQ(report->context.arch.u.arm_64.far, regs.far);

  // When the exception handle is closed (by the zx::exception destructor at
  // the end of the function), the thread will resume from the exception.  Set
  // it up to "resume" by doing an immediate thread exit.  This should make it
  // safe to assume its stack will never be used again from here on out.  (The
  // stack will also be freed by a destructor at the end of the function.)
  constexpr uint32_t kExceptionState = ZX_EXCEPTION_STATE_THREAD_EXIT;
  ASSERT_OK(exc.set_property(ZX_PROP_EXCEPTION_STATE, &kExceptionState, sizeof(kExceptionState)));
}

#ifdef __clang__
[[clang::no_sanitize("all")]]
#endif
[[noreturn]] void
DerefTaggedPtrCrash(uintptr_t arg1, uintptr_t /*arg2*/) {
  *(reinterpret_cast<int*>(arg1)) = 1;
  __builtin_trap();
}

TEST(TopByteIgnoreTests, TaggedFARSegfault) {
  // This is effectively a nullptr dereference.
  uintptr_t tagged_ptr = AddTag(0, kTestTag);
  zx_exception_report_t report = {};
  ASSERT_NO_FATAL_FAILURE(
      CatchCrash(DerefTaggedPtrCrash, tagged_ptr, /*before_start=*/nullptr, &report));
  ASSERT_EQ(report.context.arch.u.arm_64.far, tagged_ptr);
}

int gVariableToChange = 0;

void SetupWatchpoint(zx::thread& crash_thread) {
  zx_thread_state_debug_regs_t debug_regs = {};

  // Turn on this HW watchpoint.
  ARM64_DBGWCR_E_SET(&debug_regs.hw_wps[0].dbgwcr, 1);

  // The BAS bits form an 8-bit mask that filters out matches on the aligned
  // 8-byte address range indicated by the DBGWVR value based on the byte(s)
  // accessed. So setting this to 0xff ensures that any kind of access to any
  // of the 8 bytes will be trapped.
  ARM64_DBGWCR_BAS_SET(&debug_regs.hw_wps[0].dbgwcr, 0xff);

  // Only watch stores.
  ARM64_DBGWCR_LSC_SET(&debug_regs.hw_wps[0].dbgwcr, 0b10);

  // Use the untagged address. We should be able to compare against up to bit
  // 55 when doing watchpoint address comparisons. The ARM spec also requires
  // that bits 63:49 be a sign extension of bit 48 (that is, it cannot be tagged)
  // (D13.3.12).
  debug_regs.hw_wps[0].dbgwvr = reinterpret_cast<uintptr_t>(&gVariableToChange);
  ZX_ASSERT((debug_regs.hw_wps[0].dbgwvr & 0b111) == 0 &&
            "The lowest 3 bits of DBGWVR must not be set");

  ASSERT_OK(crash_thread.write_state(ZX_THREAD_STATE_DEBUG_REGS, &debug_regs, sizeof(debug_regs)));
}

TEST(TopByteIgnoreTests, TaggedFARWatchpoint) {
  uint64_t watched_addr = reinterpret_cast<uint64_t>(&gVariableToChange);

  uintptr_t tagged_ptr = AddTag(watched_addr, kTestTag);
  zx_exception_report_t report = {};
  ASSERT_NO_FATAL_FAILURE(CatchCrash(DerefTaggedPtrCrash, tagged_ptr, SetupWatchpoint, &report));
  EXPECT_EQ(report.header.type, ZX_EXCP_HW_BREAKPOINT);
  EXPECT_EQ(report.context.arch.u.arm_64.far, tagged_ptr);
}

static zx_koid_t get_object_koid(zx_handle_t handle) {
  zx_info_handle_basic_t info;
  if (zx_object_get_info(handle, ZX_INFO_HANDLE_BASIC, &info, sizeof(info), NULL, NULL) != ZX_OK) {
    return ZX_KOID_INVALID;
  }
  return info.koid;
}

void TestFutexWaitWake(uint8_t wait_tag, uint8_t wake_tag, uint8_t get_owner_tag) {
  constexpr uint32_t kThreadWakeAllCount = std::numeric_limits<uint32_t>::max();
  constexpr zx_futex_t kFutexVal = 1;
  zx_futex_t futex = kFutexVal;
  thrd_t thread;
  struct ThreadArgs {
    zx_futex_t* futex;
    std::atomic<bool> about_to_wait;
    zx_handle_t new_owner;
  };
  ThreadArgs thread_args{
      AddTag(&futex, wait_tag),
      false,
      thrd_get_zx_handle(thrd_current()),
  };

  // Start a new thread that will wait until the current thread wakes the futex.
  ASSERT_EQ(thrd_create(
                &thread,
                [](void* arg) -> int {
                  auto* thread_args = reinterpret_cast<ThreadArgs*>(arg);
                  thread_args->about_to_wait.store(true);
                  // Note that we pass in the futex value separately rather than derefing the futex
                  // pointer because, under ASan, a tagged futex pointer will cause some tag bits to
                  // spill into the rest of the pointer when calculating shadow memory if we do the
                  // dereference. This avoids us needing to do that.
                  zx_status_t status =
                      zx_futex_wait(thread_args->futex, kFutexVal, thread_args->new_owner,
                                    zx_deadline_after(ZX_TIME_INFINITE));
                  return static_cast<int>(status);
                },
                &thread_args),
            0);

  // If something goes wrong and we bail out early, do our best to shut down as cleanly
  auto cleanup = fit::defer([&]() {
    EXPECT_OK(zx_futex_wake(&futex, kThreadWakeAllCount));
    int result;
    EXPECT_EQ(thrd_join(thread, &result), 0);
    EXPECT_EQ(result, 0);
  });

  // Ensure that we're waiting on the futex before we wake it.
  zx_info_thread_t info = {};
  while (info.state != ZX_THREAD_STATE_BLOCKED_FUTEX) {
    ASSERT_OK(zx_object_get_info(thrd_get_zx_handle(thread), ZX_INFO_THREAD, &info, sizeof(info),
                                 nullptr, nullptr));
  }

  // Check the owner.
  zx_koid_t owner;
  EXPECT_OK(zx_futex_get_owner(AddTag(&futex, get_owner_tag), &owner));
  EXPECT_EQ(owner, get_object_koid(thrd_get_zx_handle(thrd_current())));
}

TEST(TopByteIgnoreTests, FutexWaitWake) {
  TestFutexWaitWake(0, 0, 0);                       // Wait and wake same futex on the same tag.
  TestFutexWaitWake(kTestTag, kTestTag, kTestTag);  // Wait and wake same futex on the same tag.
  TestFutexWaitWake(kTestTag, kTestTag + 1,
                    kTestTag + 2);  // Wait and wake same futex on different tags.
}

#ifdef __clang__
[[clang::no_sanitize("all")]]
#endif
uint8_t
UnsanitizedLoad(volatile uint8_t* ptr) {
  return *ptr;
}

TEST(TopByteIgnoreTests, VmmPageFaultHandlerDataAbort) {
  zx_handle_t root_vmar = zx_vmar_root_self();

  // Create a new vmar to manage that we will eventually decommit.
  zx_handle_t decommit_vmar;
  uintptr_t addr;
  ASSERT_OK(zx_vmar_allocate(root_vmar,
                             ZX_VM_CAN_MAP_SPECIFIC | ZX_VM_CAN_MAP_READ | ZX_VM_CAN_MAP_WRITE, 0,
                             zx_system_get_page_size() * 8, &decommit_vmar, &addr));

  // Create a vmo we can write to.
  zx_handle_t vmo;
  ASSERT_OK(zx_vmo_create(zx_system_get_page_size(), 0, &vmo));

  uint64_t mapping_addr;
  ASSERT_OK(zx_vmar_map(decommit_vmar, ZX_VM_SPECIFIC | ZX_VM_PERM_READ | ZX_VM_PERM_WRITE,
                        zx_system_get_page_size(), vmo, 0, zx_system_get_page_size(),
                        &mapping_addr));

  // We should be able to write normally.
  *reinterpret_cast<volatile uint8_t*>(mapping_addr) = 42;

  // After decommitting, the page is zero-filled. It will still be accessible, but not mapped to
  // anything. This will result in a permission fault that will be handled successfully by the
  // kernel's page fault handler. What we want to test for is that even if this pointer is tagged,
  // then the kernel will still be able to handle this page fault successfully.
  EXPECT_OK(zx_vmar_op_range(decommit_vmar, ZX_VMAR_OP_DECOMMIT, mapping_addr,
                             zx_system_get_page_size(), nullptr, 0));
  mapping_addr = AddTag(mapping_addr, kTestTag);

  // Do not do a regular dereference because ASan will right-shift the tag into the address bits
  // then complain that this address doesn't have a corresponding shadow.
  EXPECT_EQ(UnsanitizedLoad(reinterpret_cast<volatile uint8_t*>(mapping_addr)), 0);
}

arch::ArmExceptionSyndromeRegister::ExceptionClass GetEC(uint64_t esr) {
  return arch::ArmExceptionSyndromeRegister::Get().FromValue(esr).ec();
}

// Making it global static ensures this is in rodata.
static constexpr uint32_t kUdf0 = 0;

TEST(TopByteIgnoreTests, InstructionAbortNoTag) {
  // Unlike a data abort, instruction aborts on AArch64 will not include the tag in the FAR, so a
  // tag will never reach the VM layer via an instruction abort. This test verifies the FAR does not
  // include the tag in this case.
  uintptr_t pc = AddTag(reinterpret_cast<uintptr_t>(&kUdf0), kTestTag);
  zx_exception_report_t report = {};

  ASSERT_NO_FATAL_FAILURE(CatchCrash(reinterpret_cast<crash_function_t>(pc), /*arg1=*/0,
                                     /*before_start=*/nullptr, &report));
  EXPECT_EQ(report.header.type, ZX_EXCP_FATAL_PAGE_FAULT);
  ASSERT_EQ(GetEC(report.context.arch.u.arm_64.esr),
            arch::ArmExceptionSyndromeRegister::ExceptionClass::kInstructionAbortLowerEl);
  EXPECT_EQ(report.context.arch.u.arm_64.far, reinterpret_cast<uintptr_t>(&kUdf0));
}

#elif defined(__x86_64__)

TEST(TopByteIgnoreTests, AddressTaggingGetSystemFeaturesX86_64) {
  uint32_t features = 0;
  ASSERT_OK(zx_system_get_features(ZX_FEATURE_KIND_ADDRESS_TAGGING, &features));
  ASSERT_EQ(features, 0);
}

#endif

}  // namespace
