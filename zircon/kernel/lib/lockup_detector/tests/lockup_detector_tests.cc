// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/fit/defer.h>
#include <lib/lockup_detector.h>
#include <lib/lockup_detector/diagnostics.h>
#include <lib/unittest/unittest.h>

#include <kernel/auto_preempt_disabler.h>
#include <kernel/percpu.h>
#include <vm/vm.h>
#include <vm/vm_address_region.h>
#include <vm/vm_aspace.h>
#include <vm/vm_object_paged.h>

#if defined(__aarch64__)
#include <arch/arm64/dap.h>
#endif

namespace {

bool NestedCriticalSectionTest() {
  BEGIN_TEST;

  AutoPreemptDisabler ap_disabler;

  // For the context of this test, use the maximum threshold to prevent the detector from "firing".
  auto cleanup = fit::defer(
      [orig = lockup_get_cs_threshold_ticks()]() { lockup_set_cs_threshold_ticks(orig); });
  lockup_set_cs_threshold_ticks(INT64_MAX);

  const LockupDetectorState& state = gLockupDetectorPerCpuState[arch_curr_cpu_num()];
  const auto& cs_state = state.critical_section;

  EXPECT_EQ(0u, cs_state.depth);
  EXPECT_EQ(0u, cs_state.begin_ticks);

  static constexpr const char kOuter[] = "NestedCriticalSectionTest-outer";
  lockup_begin(kOuter);
  EXPECT_EQ(1u, cs_state.depth);
  EXPECT_EQ(0u, cs_state.begin_ticks);
  EXPECT_EQ(cs_state.name.load(), kOuter);

  static constexpr const char kInner[] = "NestedCriticalSectionTest-inner";
  lockup_begin(kInner);
  EXPECT_EQ(2u, cs_state.depth);
  // No change because only the outer most critical section is tracked.
  EXPECT_EQ(0u, cs_state.begin_ticks);
  EXPECT_EQ(cs_state.name.load(), kOuter);

  lockup_end();
  EXPECT_EQ(1u, cs_state.depth);
  EXPECT_EQ(0u, cs_state.begin_ticks);
  EXPECT_EQ(cs_state.name.load(), kOuter);

  lockup_end();
  EXPECT_EQ(0u, cs_state.depth);
  EXPECT_EQ(0, cs_state.begin_ticks);
  EXPECT_EQ(cs_state.name.load(), nullptr);

  END_TEST;
}

bool NestedTimedCriticalSectionTest() {
  BEGIN_TEST;

  AutoPreemptDisabler ap_disabler;

  // For the context of this test, use the maximum threshold to prevent the detector from "firing".
  auto cleanup = fit::defer(
      [orig = lockup_get_cs_threshold_ticks()]() { lockup_set_cs_threshold_ticks(orig); });
  lockup_set_cs_threshold_ticks(INT64_MAX);

  const LockupDetectorState& state = gLockupDetectorPerCpuState[arch_curr_cpu_num()];
  const auto& cs_state = state.critical_section;

  EXPECT_EQ(0u, cs_state.depth);
  EXPECT_EQ(0u, cs_state.begin_ticks);

  zx_ticks_t now = current_ticks();

  static constexpr const char kOuter[] = "NestedTimedCriticalSectionTest-outer";
  lockup_timed_begin(kOuter);
  EXPECT_EQ(1u, cs_state.depth);

  const zx_ticks_t begin_ticks = cs_state.begin_ticks;
  EXPECT_GE(cs_state.begin_ticks, now);
  EXPECT_EQ(cs_state.name.load(), kOuter);

  static constexpr const char kInner[] = "NestedTimedCriticalSectionTest-inner";
  lockup_timed_begin(kInner);
  EXPECT_EQ(2u, cs_state.depth);

  // No change because only the outer most critical section is tracked.
  EXPECT_EQ(begin_ticks, cs_state.begin_ticks);
  EXPECT_EQ(cs_state.name.load(), kOuter);

  lockup_timed_end();
  EXPECT_EQ(1u, cs_state.depth);

  EXPECT_EQ(begin_ticks, cs_state.begin_ticks);
  EXPECT_EQ(cs_state.name.load(), kOuter);

  lockup_timed_end();
  EXPECT_EQ(0u, cs_state.depth);
  EXPECT_EQ(0, cs_state.begin_ticks);
  EXPECT_EQ(cs_state.name.load(), nullptr);

  END_TEST;
}

bool GetBacktraceFromDapStateTest() {
  BEGIN_TEST;

#if !defined(__aarch64__)
  printf("this is an arm64-only test, skipping\n");
  END_TEST;

#else

  constexpr uint64_t kPc = 0xffffffff10000000;
  constexpr uint64_t kLr = 0xffffffff10000001;
  constexpr uint64_t kEdscrEl0 = 0x3053a13;
  constexpr uint64_t kEdscrEl1 = 0x3053d13;

  // CPU is in EL0.
  {
    arm64_dap_processor_state state{};
    state.pc = kPc;
    state.r[30] = kLr;
    state.edscr = kEdscrEl0;
    Backtrace bt;
    ASSERT_EQ(ZX_ERR_BAD_STATE, lockup_internal::GetBacktraceFromDapState(state, bt));
    ASSERT_EQ(0u, bt.size());
  }

  auto check_backtrace = [&](const Backtrace& bt, size_t size, vaddr_t slot0, vaddr_t slot1,
                             vaddr_t top_of_stack_value) -> bool {
    BEGIN_TEST;
    ASSERT_EQ(size, bt.size());
    EXPECT_EQ(slot0, bt.data()[0]);
    EXPECT_EQ(slot1, bt.data()[1]);
    for (uint64_t i = 2; i < size; ++i) {
      EXPECT_EQ(top_of_stack_value, bt.data()[i]);
      --top_of_stack_value;
    }
    END_TEST;
  };

  // Misaligned SCSP.
  {
    arm64_dap_processor_state state{};
    state.pc = kPc;
    state.r[30] = kLr;
    state.edscr = kEdscrEl1;
    state.r[18] = 0xffff0000172cc4b1;
    Backtrace bt;
    ASSERT_EQ(ZX_ERR_INVALID_ARGS, lockup_internal::GetBacktraceFromDapState(state, bt));
    EXPECT_TRUE(check_backtrace(bt, 2, kPc, kLr, 0));
  }

  // Null SCSP.
  {
    arm64_dap_processor_state state{};
    state.pc = kPc;
    state.r[30] = kLr;
    state.edscr = kEdscrEl1;
    state.r[18] = 0x0;
    Backtrace bt;
    ASSERT_EQ(ZX_ERR_INVALID_ARGS, lockup_internal::GetBacktraceFromDapState(state, bt));
    EXPECT_TRUE(check_backtrace(bt, 2, kPc, kLr, 0));
  }

  // SCSP is not a kernel address.
  {
    arm64_dap_processor_state state{};
    state.pc = kPc;
    state.r[30] = kLr;
    state.edscr = kEdscrEl1;
    state.r[18] = 0xdc050800;
    ASSERT_TRUE(is_user_address(state.r[18]));
    Backtrace bt;
    ASSERT_EQ(ZX_ERR_OUT_OF_RANGE, lockup_internal::GetBacktraceFromDapState(state, bt));
    EXPECT_TRUE(check_backtrace(bt, 2, kPc, kLr, 0));
  }

  // Create a region of four pages.  The middle two are mapped and the ends are "holes".
  constexpr size_t kRegionSize = PAGE_SIZE * 4;
  constexpr size_t kVmoSize = PAGE_SIZE * 2;
  constexpr uint32_t kVmarFlags =
      VMAR_FLAG_CAN_MAP_SPECIFIC | VMAR_FLAG_CAN_MAP_READ | VMAR_FLAG_CAN_MAP_WRITE;
  constexpr char kName[] = "lockup_detector test";
  fbl::RefPtr<VmAddressRegion> root_vmar =
      VmAspace::kernel_aspace()->RootVmar()->as_vm_address_region();
  fbl::RefPtr<VmAddressRegion> vmar;
  ASSERT_OK(root_vmar->CreateSubVmar(0, kRegionSize, 0, kVmarFlags, kName, &vmar));
  auto vmar_cleanup = fit::defer([&vmar]() { vmar->Destroy(); });

  // Create a VMO of two pages and map it in the middle.
  //
  //         mapping.base()
  //         V
  // [-hole-][page-1][page-2][-hole-]
  // ^
  // vmar.base()
  //
  fbl::RefPtr<VmObjectPaged> vmo;
  ASSERT_OK(VmObjectPaged::Create(PMM_ALLOC_FLAG_ANY, 0u, kVmoSize, &vmo));
  fbl::RefPtr<VmMapping> mapping;
  ASSERT_OK(vmar->CreateVmMapping(PAGE_SIZE, kVmoSize, 0, VMAR_FLAG_SPECIFIC, ktl::move(vmo), 0,
                                  ARCH_MMU_FLAG_PERM_READ | ARCH_MMU_FLAG_PERM_WRITE, kName,
                                  &mapping));
  // Eagerly fault in the pages.
  ASSERT_OK(mapping->MapRange(0, kVmoSize, true));

  // Fill the two middle pages with some "return addresses".
  const size_t num_elements = kVmoSize / sizeof(vaddr_t);
  auto* p = reinterpret_cast<vaddr_t*>(mapping->base());
  for (uint64_t i = 0; i < num_elements; ++i) {
    p[i] = i;
  }

  // SCSP points to middle of an unmapped region.
  //
  // [-hole-][page-1][page-2][-hole-]
  //    ^
  {
    arm64_dap_processor_state state{};
    state.pc = kPc;
    state.r[30] = kLr;
    state.edscr = kEdscrEl1;
    state.r[18] = vmar->base() + 64;
    ASSERT_TRUE(is_kernel_address(state.r[18]));
    Backtrace bt;
    ASSERT_EQ(ZX_ERR_NOT_FOUND, lockup_internal::GetBacktraceFromDapState(state, bt));
    EXPECT_TRUE(check_backtrace(bt, 2, kPc, kLr, 0));
  }

  // auto elem_index = [](uintptr_t r18, uintptr_t vmar_base) -> size_t {
  //   return (r18 - (vmar_base + PAGE_SIZE)) / sizeof(vaddr_t) - 1;
  // };

  // SCSP points to the first address of an unmapped page that follows a mapped page.
  //
  // [-hole-][page-1][page-2][-hole-]
  //                         ^
  {
    arm64_dap_processor_state state{};
    state.pc = kPc;
    state.r[30] = kLr;
    state.edscr = kEdscrEl1;
    state.r[18] = vmar->base() + PAGE_SIZE * 3;
    ASSERT_TRUE(is_kernel_address(state.r[18]));
    Backtrace bt;
    // See that we get a full backtrace.  The fact that SCSP pointed at an unmapped page didn't
    // matter because it was post-increment semantics (it was pointing at an empty slot).
    ASSERT_EQ(ZX_OK, lockup_internal::GetBacktraceFromDapState(state, bt));
    const vaddr_t top_of_stack_value = reinterpret_cast<vaddr_t*>(state.r[18])[-1];
    EXPECT_TRUE(check_backtrace(bt, Backtrace::kMaxSize, kPc, kLr, top_of_stack_value));
  }

  // SCS crosses a page boundary.  See that the backtrace does not.
  //
  // [-hole-][page-1][page-2][-hole-]
  //                  ^
  {
    arm64_dap_processor_state state{};
    state.pc = kPc;
    state.r[30] = kLr;
    state.edscr = kEdscrEl1;
    state.r[18] = vmar->base() + PAGE_SIZE * 2 + 16;
    ASSERT_TRUE(is_kernel_address(state.r[18]));
    Backtrace bt;
    ASSERT_EQ(ZX_OK, lockup_internal::GetBacktraceFromDapState(state, bt));
    const vaddr_t top_of_stack_value = reinterpret_cast<vaddr_t*>(state.r[18])[-1];
    EXPECT_TRUE(check_backtrace(bt, 4, kPc, kLr, top_of_stack_value));
  }

  // Unmapped page, followed by small SCS (less than Backtrace::kMaxSize).
  //
  // [-hole-][page-1][page-2][-hole-]
  //           ^
  {
    arm64_dap_processor_state state{};
    state.pc = kPc;
    state.r[30] = kLr;
    state.edscr = kEdscrEl1;
    state.r[18] = vmar->base() + PAGE_SIZE + 16;
    ASSERT_TRUE(is_kernel_address(state.r[18]));
    Backtrace bt;
    ASSERT_EQ(ZX_OK, lockup_internal::GetBacktraceFromDapState(state, bt));
    const vaddr_t top_of_stack_value = reinterpret_cast<vaddr_t*>(state.r[18])[-1];
    EXPECT_TRUE(check_backtrace(bt, 4, kPc, kLr, top_of_stack_value));
  }

#endif

  END_TEST;
}

}  // namespace

UNITTEST_START_TESTCASE(lockup_detetcor_tests)
UNITTEST("nested_critical_section", NestedCriticalSectionTest)
UNITTEST("nested_timed_critical_section", NestedTimedCriticalSectionTest)
UNITTEST("get_backtrace_from_dap_state", GetBacktraceFromDapStateTest)
UNITTEST_END_TESTCASE(lockup_detetcor_tests, "lockup_detector", "lockup_detector tests")
