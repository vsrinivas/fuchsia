// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/arch/arm64/system.h>
#include <lib/elfldltl/machine.h>
#include <lib/fit/defer.h>
#include <lib/zircon-internal/default_stack_size.h>
#include <lib/zx/exception.h>
#include <lib/zx/job.h>
#include <lib/zx/process.h>
#include <lib/zx/thread.h>
#include <zircon/features.h>
#include <zircon/hw/debug/arm64.h>
#include <zircon/syscalls.h>
#include <zircon/syscalls/debug.h>
#include <zircon/syscalls/exception.h>
#include <zircon/threads.h>

#include <algorithm>

#include <mini-process/mini-process.h>
#include <zxtest/zxtest.h>

namespace {

#if defined(__aarch64__)

constexpr size_t kTagShift = 56;
constexpr uint8_t kTestTag = 0xAB;
constexpr size_t kThreadStackSize = ZIRCON_DEFAULT_STACK_SIZE;
constexpr uint64_t kTagMask = UINT64_C(0xff) << kTagShift;

// Add a tag to the pointer if the pointer is untagged. An optional tag value
// can be passed, and if one is, it will override the current tag.
//
// Under normal untagged use cases, this can be used for just adding an
// arbitrary tag value to a pointer. If hwasan is enabled, the pointer may
// already be tagged and will remain unchanged, unless a tag value is provided
// to override it. In that case, users of this function should be careful this
// doesn't lead to hwasan false-positives with tag-checking. Ideal cases where
// one might want to override the tag if hwasan is present are for ensuring
// that two pointers have different tags, since hwasan could technically but
// unlikely produce the same tag for different pointers.
constexpr uint64_t AddTagIfNeeded(uintptr_t ptr, uint8_t* newtag = nullptr) {
  if (newtag) {
    // Add the tag or overwrite it if there is one.
    return (static_cast<uint64_t>(*newtag) << kTagShift) | (static_cast<uint64_t>(ptr) & ~kTagMask);
  }
  if (kTagMask & ptr) {
    // There already exists a tag.
    return ptr;
  }
  // Add the default test tag.
  return (static_cast<uint64_t>(kTestTag) << kTagShift) | static_cast<uint64_t>(ptr);
}

template <typename T>
T* AddTagIfNeeded(T* ptr, uint8_t* newtag = nullptr) {
  return reinterpret_cast<T*>(AddTagIfNeeded(reinterpret_cast<uintptr_t>(ptr), newtag));
}

constexpr uintptr_t RemoveTag(uintptr_t ptr) { return ptr & ~kTagMask; }

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
  DerefTaggedPtr(AddTagIfNeeded(&val));
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
  const uintptr_t sp = elfldltl::AbiTraits<>::InitialStackPointer(
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
  zx_signals_t pending;
  ASSERT_OK(exception_channel.wait_one(ZX_CHANNEL_READABLE | ZX_CHANNEL_PEER_CLOSED,
                                       zx::time::infinite(), &pending));
  ZX_ASSERT_MSG(pending & ZX_CHANNEL_READABLE, "exception channel peer closed");

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

TEST(TopByteIgnoreTests, VmarTaggedAddress) {
  // Write pattern via VMO and read it via zx_process_read_memory(). Each address argument in these
  // syscalls must not be tagged, but user pointers can be tagged.
  uint8_t buff[] = {1, 2, 3, 4};
  constexpr size_t kVmoSize = sizeof(buff);
  constexpr size_t kVmarSize = 4096;  // Must be page-aligned.
  constexpr zx_vm_option_t kVmarOpts = ZX_VM_CAN_MAP_READ | ZX_VM_CAN_MAP_WRITE;
  constexpr zx_vm_option_t kMapOpts = ZX_VM_PERM_READ | ZX_VM_PERM_WRITE;

  // Setup the VMO. User pointers provided to syscalls can be tagged and work properly.
  zx::vmo vmo;
  zx::vmar vmar;
  zx_vaddr_t vmar_addr, map_addr;
  ASSERT_OK(zx_vmo_create(kVmoSize, 0u, AddTagIfNeeded(vmo.reset_and_get_address())));
  ASSERT_OK(zx_vmar_allocate(zx_vmar_root_self(), kVmarOpts, 0u, kVmarSize,
                             AddTagIfNeeded(vmar.reset_and_get_address()),
                             AddTagIfNeeded(&vmar_addr)));
  ASSERT_OK(vmar.map(kMapOpts, 0u, vmo, 0u, kVmoSize, AddTagIfNeeded(&map_addr)));

  // Note that the mapopts were set when mapping meaning this would be a no-op,
  // but this just checks we can't tag vmar_protect regardless.
  ASSERT_STATUS(vmar.protect(kMapOpts, AddTagIfNeeded(map_addr), kVmarSize), ZX_ERR_INVALID_ARGS);
  ASSERT_OK(vmar.protect(kMapOpts, map_addr, kVmarSize));

  auto IsUntagged = [](uintptr_t ptr) { return (ptr >> kTagShift) == 0; };
  ASSERT_TRUE(IsUntagged(vmar_addr));
  ASSERT_TRUE(IsUntagged(map_addr));

  size_t actual = 0u;

  // Write via the VMO...
  ASSERT_OK(vmo.write(AddTagIfNeeded(buff), 0u, kVmoSize));

  // ...then read via zx_process_read_memory. The kernel will treat a tagged vmar address normally,
  // but fail when it sees there's no memory at the tagged address.
  auto buf = std::make_unique<uint8_t[]>(kVmoSize);
  ASSERT_STATUS(
      zx::process::self()->read_memory(AddTagIfNeeded(vmar_addr), AddTagIfNeeded(buf.get()),
                                       kVmoSize, AddTagIfNeeded(&actual)),
      ZX_ERR_NO_MEMORY);
  ASSERT_OK(zx::process::self()->read_memory(vmar_addr, AddTagIfNeeded(buf.get()), kVmoSize,
                                             AddTagIfNeeded(&actual)));
  ASSERT_EQ(actual, kVmoSize);
  ASSERT_EQ(memcmp(buf.get(), buff, kVmoSize), 0);

  // Shuffle the data that will be written.
  std::reverse(buff, buff + kVmoSize);

  // Now write via zx_process_write_memory...
  ASSERT_STATUS(zx::process::self()->write_memory(AddTagIfNeeded(vmar_addr), AddTagIfNeeded(buff),
                                                  kVmoSize, AddTagIfNeeded(&actual)),
                ZX_ERR_NO_MEMORY);
  ASSERT_OK(zx::process::self()->write_memory(vmar_addr, AddTagIfNeeded(buff), kVmoSize,
                                              AddTagIfNeeded(&actual)));
  ASSERT_EQ(actual, kVmoSize);

  // ...then read via the VMO.
  ASSERT_OK(vmo.read(AddTagIfNeeded(buf.get()), 0u, kVmoSize));
  ASSERT_EQ(memcmp(buf.get(), buff, kVmoSize), 0);

  // We're done with the vmo and vmar. Although they will be destroyed after
  // exiting this scope, we can do some checks here on syscalls for unmapping and
  // decommitting.
  ASSERT_STATUS(vmar.op_range(ZX_VMO_OP_DECOMMIT, AddTagIfNeeded(map_addr), kVmarSize, nullptr, 0u),
                ZX_ERR_OUT_OF_RANGE);
  ASSERT_OK(vmar.op_range(ZX_VMO_OP_DECOMMIT, map_addr, kVmarSize, nullptr, 0u));

  ASSERT_STATUS(vmar.unmap(AddTagIfNeeded(vmar_addr), kVmarSize), ZX_ERR_INVALID_ARGS);
  ASSERT_OK(vmar.unmap(vmar_addr, kVmarSize));
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
  uintptr_t tagged_ptr = AddTagIfNeeded(0);
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

  ASSERT_OK(crash_thread.write_state(ZX_THREAD_STATE_DEBUG_REGS, &debug_regs, sizeof(debug_regs)));
}

TEST(TopByteIgnoreTests, TaggedFARWatchpoint) {
  uint64_t watched_addr = reinterpret_cast<uint64_t>(&gVariableToChange);

  uintptr_t tagged_ptr = AddTagIfNeeded(watched_addr);
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
      // Manually add the tag here which may be different from the one in the pointer passed to
      // `zx_futex_get_owner`. This tag should be irrelevant on a futex comparison.
      AddTagIfNeeded(&futex, &wait_tag),
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
  // Manually add the tag here which may be different from the one in the initial `thread_args`.
  // This tag should be irrelevant on a futex comparison.
  EXPECT_OK(zx_futex_get_owner(AddTagIfNeeded(&futex, &get_owner_tag), &owner));
  EXPECT_EQ(owner, get_object_koid(thrd_get_zx_handle(thrd_current())));
}

TEST(TopByteIgnoreTests, FutexWaitWake) {
  // These tags are manually included in futex pointers passed to futex syscalls. The actual tag
  // values don't matter as long as we can test they work as intended if they're the same or
  // different.
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
  mapping_addr = AddTagIfNeeded(mapping_addr);

  // Do not do a regular dereference because ASan will right-shift the tag into the address bits
  // then complain that this address doesn't have a corresponding shadow.
  EXPECT_EQ(UnsanitizedLoad(reinterpret_cast<volatile uint8_t*>(mapping_addr)), 0);
}

arch::ArmExceptionSyndromeRegister::ExceptionClass GetEC(uint64_t esr) {
  return arch::ArmExceptionSyndromeRegister::Get().FromValue(esr).ec();
}

// Making it global static ensures this is in rodata.
const uint32_t kUdf0 = 0;

TEST(TopByteIgnoreTests, InstructionAbortNoTag) {
  // Unlike a data abort, instruction aborts on AArch64 will not include the tag in the FAR, so a
  // tag will never reach the VM layer via an instruction abort. This test verifies the FAR does not
  // include the tag in this case.
  uintptr_t pc = AddTagIfNeeded(reinterpret_cast<uintptr_t>(&kUdf0));
  zx_exception_report_t report = {};

  ASSERT_NO_FATAL_FAILURE(CatchCrash(reinterpret_cast<crash_function_t>(pc), /*arg1=*/0,
                                     /*before_start=*/nullptr, &report));
  EXPECT_EQ(report.header.type, ZX_EXCP_FATAL_PAGE_FAULT);
  ASSERT_EQ(GetEC(report.context.arch.u.arm_64.esr),
            arch::ArmExceptionSyndromeRegister::ExceptionClass::kInstructionAbortLowerEl);
  EXPECT_EQ(report.context.arch.u.arm_64.far, RemoveTag(reinterpret_cast<uintptr_t>(&kUdf0)));
}

#ifdef __clang__
[[clang::no_sanitize("all")]]
#endif
__NO_RETURN void
DoNothing() {
  zx_thread_exit();
}

TEST(TopByteIgnoreTests, ThreadStartTaggedAddress) {
  std::unique_ptr<std::byte[]> thread_stack = std::make_unique<std::byte[]>(kThreadStackSize);
  const uintptr_t pc = reinterpret_cast<uintptr_t>(DoNothing);
  const uintptr_t sp = elfldltl::AbiTraits<>::InitialStackPointer(
      reinterpret_cast<uintptr_t>(thread_stack.get()), kThreadStackSize);

  auto run_thread = [](uintptr_t pc, uintptr_t sp) {
    constexpr std::string_view kThreadName = "TBI tagged entry/stack";
    zx::thread thread;
    ASSERT_OK(zx::thread::create(*zx::process::self(), kThreadName.data(), kThreadName.size(), 0,
                                 &thread));

    ASSERT_OK(thread.start(pc, sp, 0, 0));
    zx_signals_t observed;
    ASSERT_OK(
        thread.wait_one(ZX_THREAD_TERMINATED, zx::time::infinite(), AddTagIfNeeded(&observed)));
    ASSERT_TRUE(observed & ZX_THREAD_TERMINATED);
  };

  // Both the PC and SP can be tagged.
  run_thread(AddTagIfNeeded(pc), sp);
  run_thread(pc, AddTagIfNeeded(sp));
}

TEST(TopByteIgnoreTests, ProcessStartTaggedAddress) {
  auto run_process = [](uint8_t pc_tag, uint8_t sp_tag) {
    zx::process proc;
    zx::thread thread;
    zx::vmar vmar;

    constexpr std::string_view kTestName = "TBI process";
    ASSERT_OK(zx::process::create(*zx::job::default_job(), kTestName.data(), kTestName.size(), 0,
                                  &proc, &vmar));
    ASSERT_OK(zx::thread::create(proc, kTestName.data(), kTestName.size(), 0, &thread));

    // The process will get no handles, but it can still make syscalls.
    // The vDSO's e_entry points to zx_process_exit.  So the process will
    // enter at `zx_process_exit(ZX_HANDLE_INVALID);`.
    uintptr_t entry;
    EXPECT_OK(mini_process_load_vdso(proc.get(), vmar.get(), nullptr, &entry));

    // The vDSO ABI needs a stack, though zx_process_exit actually might not.
    uintptr_t stack_base, sp;
    EXPECT_OK(mini_process_load_stack(vmar.get(), false, &stack_base, &sp));
    zx_handle_close(vmar.get());

    ASSERT_OK(proc.start(thread, AddTagIfNeeded(entry, &pc_tag), AddTagIfNeeded(sp, &sp_tag),
                         zx::handle(), 0));

    zx_signals_t signals;
    EXPECT_OK(proc.wait_one(ZX_TASK_TERMINATED, zx::deadline_after(zx::sec(1)), &signals));
    EXPECT_EQ(signals, ZX_TASK_TERMINATED);
  };

  run_process(kTestTag, 0);
  run_process(0, kTestTag);
}

#elif defined(__x86_64__)

TEST(TopByteIgnoreTests, AddressTaggingGetSystemFeaturesX86_64) {
  uint32_t features = 0;
  ASSERT_OK(zx_system_get_features(ZX_FEATURE_KIND_ADDRESS_TAGGING, &features));
  ASSERT_EQ(features, 0);
}

#endif

}  // namespace
