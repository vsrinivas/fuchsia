// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_LIB_ARCH_INCLUDE_LIB_ARCH_X86_LBR_H_
#define ZIRCON_KERNEL_LIB_ARCH_INCLUDE_LIB_ARCH_X86_LBR_H_

#include <lib/arch/x86/cpuid.h>
#include <lib/arch/x86/trace.h>
#include <zircon/types.h>

#include <optional>
#include <type_traits>
#include <utility>

#include <hwreg/bitfields.h>

namespace arch {

// [intel/vol3]: Table 17-13.  MSR_LBR_SELECT for Intel® microarchitecture
// code name Haswell.
//
// MSR_LBR_SELECT.
//
// Control LBR filtering.
//
// Though the referenced section is for Haswell, the layout is generically
// accurate, modulo EN_CALLSTACK (see note below).
struct LbrSelectMsr : public X86MsrBase<LbrSelectMsr, X86Msr::MSR_LBR_SELECT> {
  DEF_RSVDZ_FIELD(63, 10);
  // This field is actually on present on Atom microarchitectures post-Goldmont
  // and and Core microarchitectures post-Haswell; it is otherwise reserved.
  // Care should be taken only to use this field when present.
  DEF_BIT(9, en_callstack);
  DEF_BIT(8, far_branch);
  DEF_BIT(7, near_rel_jmp);
  DEF_BIT(6, near_ind_jmp);
  DEF_BIT(5, near_ret);
  DEF_BIT(4, near_ind_call);
  DEF_BIT(3, near_rel_call);
  DEF_BIT(2, jcc);
  DEF_BIT(1, cpl_neq_0);
  DEF_BIT(0, cpl_eq_0);
};

// [intel/vol3]: 17.4.8  LBR Stack.
//
// MSR_LASTBRANCH_TOS.
//
// Points to the "top" of the LBR stack.
struct LbrTopOfStackMsr : public X86MsrBase<LbrTopOfStackMsr, X86Msr::MSR_LASTBRANCH_TOS> {
  // Gives the index of the most recent branch record, in turn given by bits
  // [stack_size:0] of the register value. We unfortunately cannot trust the
  // higher bits to be reserved as zero as that is not expressly documented.
  size_t top(size_t stack_size) { return static_cast<size_t>(reg_value()) & (stack_size - 1); }
};

// [intel/vol3]: 17.4.8.1  LBR Stack and Intel® 64 Processors.
//
// MSR_LASTBRANCH_N_FROM_IP.
//
// Pointer the source instruction in a branch possibly along with metadata.
class LbrFromIpMsr : public hwreg::RegisterBase<LbrFromIpMsr, uint64_t> {
 public:
  uint64_t ip(X86LbrFormat format) const;

  // Metadata accessors return optional data, as the latter are only present
  // in particular (older) formats; on newer microarchictures it is expected
  // that this information can be found in the MSR_LBR_INFO_* MSRs instead.
  std::optional<bool> tsx_abort(X86LbrFormat format) const;
  std::optional<bool> in_tsx(X86LbrFormat format) const;
  std::optional<bool> mispredicted(X86LbrFormat format) const;

  // Returns the value associated with MSR_LASTBRANCH_N_FROM_IP.
  static auto Get(size_t N) {
    return hwreg::RegisterAddr<LbrFromIpMsr>(
        static_cast<uint32_t>(X86Msr::MSR_LASTBRANCH_0_FROM_IP) + static_cast<uint32_t>(N));
  }

 private:
  DEF_FIELD(63, 0, modern_ip);
  DEF_FIELD(62, 0, legacy_without_tsx_ip);
  DEF_FIELD(60, 0, legacy_with_tsx_ip);
  DEF_BIT(63, legacy_mispredicted);
  DEF_BIT(62, legacy_in_tsx);
  DEF_BIT(61, legacy_tsx_abort);
};

// [intel/vol3]: 17.4.8.1  LBR Stack and Intel® 64 Processors.
//
// MSR_LASTBRANCH_N_TO_IP.
// Pointer the destination instruction in a branch, possibly along with
// metadata.
class LbrToIpMsr : public hwreg::RegisterBase<LbrToIpMsr, uint64_t> {
 public:
  uint64_t ip(X86LbrFormat format) const;

  // Optional, as this field is only present in particular (older) formats; on
  // newer microarchictures it is expected that this information can be found
  // in found in the MSR_LBR_INFO_* MSRs instead.
  std::optional<uint16_t> cycle_count(X86LbrFormat format) const;

  // Returns the value associated with MSR_LASTBRANCH_N_TO_IP.
  static auto Get(size_t N) {
    return hwreg::RegisterAddr<LbrToIpMsr>(static_cast<uint32_t>(X86Msr::MSR_LASTBRANCH_0_TO_IP) +
                                           static_cast<uint32_t>(N));
  }

 private:
  DEF_FIELD(63, 0, modern_ip);
  DEF_FIELD(63, 48, legacy_cycle_count);
  DEF_FIELD(47, 0, legacy_ip);
};

// [intel/vol3]: Table 17-16.  MSR_LBR_INFO_x.
//
// MSR_LBR_INFO_N: Additional branch metadata.
//
// Though the referenced section is for Haswell, the layout is generically
// accurate.
struct LbrInfoMsr : public hwreg::RegisterBase<LbrInfoMsr, uint64_t> {
  DEF_BIT(63, mispred);
  DEF_BIT(62, in_tsx);
  DEF_BIT(61, tsx_abort);
  // Bits [60:16] are reserved.
  DEF_FIELD(15, 0, cycle_count);

  // Returns the value associated with MSR_LBR_INFO_N.
  static auto Get(size_t N) {
    return hwreg::RegisterAddr<LbrInfoMsr>(static_cast<uint32_t>(X86Msr::MSR_LBR_INFO_0) +
                                           static_cast<uint32_t>(N));
  }
};

// A simple synthesis of the information provided by the TO, FROM, and INFO
// MSRS.
struct LastBranchRecord {
  zx_vaddr_t from;
  zx_vaddr_t to;
  // Whether the branch was mispredicted.
  std::optional<bool> mispredicted;
  // Elapsed core clocks since the last update to the LBR stack.
  std::optional<uint16_t> cycle_count;
  // Whether the branch entry occurred in a TSX (Transactional Synchronization
  // Extension) region.
  std::optional<bool> in_tsx;
  // As above, but also whether a transaction was aborted.
  std::optional<bool> tsx_abort;
};

// LbrStack provides access to the underlying Last Branch Record stack for the
// current CPU. It provides means of enabling, disabling, and iterating over
// the current records. The lifetime of this class has no bearing on that of
// the hardware feature. In principle, the same `LbrStack` could be used to
// access branch records on multiple CPUs.
//
// This abstraction provides no thread safety; it is a thin wrapper around
// access to the current LBR stack's hardware interface.
//
// Example usage (dumping kernel branch records):
// ```
//   hwreg::X86MrsIo msr;
//   LbrStack lbr_stack;
//   DEBUG_ASSERT(lbr_stack.is_supported());
//   DEBUG_ASSERT(lbr_stack.is_enabled(msr));  // Previously enabled.
//
//   PrintfSymbolizerContext(stdout);
//   printf("Last kernel branch records:\n");
//   lbr_stack.ForEachRecord(msr, [](const LastBranchRecord& lbr) {
//     // Only include branches that end in the kernel.
//     if (is_kernel_address(lbr.to)) {
//       printf("from: {{{pc:%#" PRIxPTR "}}}\n", lbr.from);
//       printf("to: {{{pc:%#" PRIxPTR "}}}\n", lbr.to);
//     }
//   });
// ```
class LbrStack {
 public:
  template <typename CpuidIoProvider,
            // To avoid precedence over copy and move constructors.
            typename = std::enable_if_t<!std::is_same_v<CpuidIoProvider, LbrStack>>>
  explicit LbrStack(CpuidIoProvider&& cpuid)
      : LbrStack(GetMicroarchitecture(cpuid), std::forward<CpuidIoProvider>(cpuid)) {}

  LbrStack() = delete;

  LbrStack(const LbrStack&) = default;
  LbrStack(LbrStack&&) = default;

  // Gives the size (or depth) of the LBR stack, which is the maximum number
  // of records that can be stored.
  size_t size() const { return size_; }

  bool is_supported() const { return supported_; }

  template <typename MsrIoProvider>
  bool is_enabled(MsrIoProvider&& msr) const {
    return supported_ && DebugControlMsr::Get().ReadFrom(&msr).lbr();
  }

  // Enables the recording of LBRs on the current CPU with a set of default
  // options (e.g., for callstack profiling when available). If |for_user| is
  // true, only records that end in CPL > 0 will be recorded. This operation is
  // idempotent.
  template <typename MsrIoProvider>
  void Enable(MsrIoProvider&& msr, bool for_user) const {
    ZX_ASSERT(supported_);
    DebugControlMsr::Get().ReadFrom(&msr).set_lbr(1).set_freeze_lbr_on_pmi(1).WriteTo(&msr);
    GetDefaultSettings(for_user).WriteTo(&msr);
  }

  // Disables the recording of LBRs on the current CPU. This operation is
  // idempotent.
  template <typename MsrIoProvider>
  void Disable(MsrIoProvider&& msr) const {
    ZX_ASSERT(supported_);
    DebugControlMsr::Get().ReadFrom(&msr).set_lbr(0).WriteTo(&msr);
  }

  // Calls each record on a provided callback. LbrStack must be enabled when
  // this method is called.
  template <typename MsrIoProvider, typename LbrCallback>
  void ForEachRecord(MsrIoProvider&& msr, LbrCallback&& callback) const {
    static_assert(std::is_invocable_r_v<void, LbrCallback, const LastBranchRecord&>);
    ZX_ASSERT(is_enabled(msr));

    X86LbrFormat format = PerfCapabilitiesMsr::Get().ReadFrom(&msr).lbr_fmt();
    size_t top = LbrTopOfStackMsr::Get().ReadFrom(&msr).top(size_);
    for (size_t i = 0; i < size_; ++i) {
      size_t idx = (top + i) % size_;
      LbrFromIpMsr from = LbrFromIpMsr::Get(idx).ReadFrom(&msr);
      LbrToIpMsr to = LbrToIpMsr::Get(idx).ReadFrom(&msr);
      LastBranchRecord record{
          .from = static_cast<zx_vaddr_t>(from.ip(format)),
          .to = static_cast<zx_vaddr_t>(to.ip(format)),
          .mispredicted = from.mispredicted(format),
          .cycle_count = to.cycle_count(format),
          .in_tsx = from.in_tsx(format),
          .tsx_abort = from.tsx_abort(format),
      };
      // The *Info formats expect all metadata to be found in the info MSRs;
      // defer evaluation until we are in such a case, as only then will we
      // know that the latter are supported.
      if (format == X86LbrFormat::k64BitEipWithInfo || format == X86LbrFormat::k64BitLipWithInfo) {
        LbrInfoMsr info = LbrInfoMsr::Get(idx).ReadFrom(&msr);
        record.mispredicted = info.mispred();
        record.cycle_count = info.cycle_count();
        record.in_tsx = info.in_tsx();
        record.tsx_abort = info.tsx_abort();
      }
      std::forward<LbrCallback>(callback)(record);
    }
  }

 private:
  // Exists so that we may initialize members as const in an initializer list.
  template <typename CpuidIoProvider>
  LbrStack(Microarchitecture microarch, CpuidIoProvider&& cpuid)
      : size_(Size(microarch)),
        supported_(size_ > 0 && PerfCapabilitiesMsr::IsSupported(cpuid)),
        callstack_profiling_(SupportsCallstackProfiling(microarch)) {}

  static size_t Size(Microarchitecture microarch);
  static bool SupportsCallstackProfiling(Microarchitecture microarch);

  // A reasonable set of default settings (e.g., excluding returns and other
  // information implicitly found in a backtrace), enablind callstack profiling
  // (see description of `callstack_profiling_` when appropriate). Revisit this
  // set of choices when we have use-cases for variations.
  LbrSelectMsr GetDefaultSettings(bool for_user) const;

  const size_t size_;
  const bool supported_;
  // Whether we can automatically flush records from the on-chip registers (in
  // a LIFO manner) when return instructions are executed, discarding branch
  // information relative to leaf functions. `[intel/v3] 17.11` gives the
  // description.
  const bool callstack_profiling_;
};

}  // namespace arch

#endif  // ZIRCON_KERNEL_LIB_ARCH_INCLUDE_LIB_ARCH_X86_LBR_H_
