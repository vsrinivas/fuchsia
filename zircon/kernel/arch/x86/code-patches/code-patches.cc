// Copyright 2021 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/arch/cache.h>
#include <lib/arch/x86/boot-cpuid.h>
#include <lib/arch/x86/bug.h>
#include <lib/boot-options/boot-options.h>
#include <lib/code-patching/code-patches.h>
#include <zircon/assert.h>

#include <cstdint>
#include <cstdio>

#include <arch/code-patches/case-id.h>
#include <arch/x86/cstring/selection.h>
#include <arch/x86/retpoline/selection.h>
#include <arch/x86/user-copy/selection.h>
#include <hwreg/x86msr.h>

namespace {

// TODO(68585): While .code-patches is allocated and accessed from directly
// within the kernel, we expect its recorded addresses to be the final,
// link-time ones.
ktl::span<ktl::byte> GetInstructions(uint64_t range_start, size_t range_size) {
  return {reinterpret_cast<ktl::byte*>(range_start), range_size};
}

void PrintCaseInfo(const code_patching::Directive& patch, const char* fmt, ...) {
  printf("code-patching: ");
  va_list args;
  va_start(args, fmt);
  vprintf(fmt, args);
  va_end(args);
  printf(": [%#lx, %#lx)\n", patch.range_start, patch.range_start + patch.range_size);
}

}  // namespace

// Declared in <lib/code-patching/code-patches.h>.
void ArchPatchCode(ktl::span<const code_patching::Directive> patches) {
  arch::BootCpuidIo cpuid;
  hwreg::X86MsrIo msr;

  // Will effect instruction-data cache consistency on destruction.
  arch::GlobalCacheConsistencyContext sync_ctx;

  for (const code_patching::Directive& patch : patches) {
    ktl::span<ktl::byte> insns = GetInstructions(patch.range_start, patch.range_size);
    if (insns.empty()) {
      ZX_PANIC("code-patching: unrecognized address range for patch case ID %u: [%#lx, %#lx)",
               patch.id, patch.range_start, patch.range_start + patch.range_size);
    }

    switch (patch.id) {
      case CASE_ID_SWAPGS_MITIGATION: {
        // `nop` out the mitigation if the bug is not present, if we could not
        // mitigate it even if it was, or if we generally want mitigations off.
        const bool present = arch::HasX86SwapgsBug(cpuid);
        if (!present || gBootOptions->x86_disable_spec_mitigations) {
          code_patching::NopFill(insns);
          ktl::string_view qualifier = !present ? "bug not present" : "all mitigations disabled";
          PrintCaseInfo(patch, "swapgs bug mitigation disabled (%V)", qualifier);
          break;
        }
        PrintCaseInfo(patch, "swapgs bug mitigation enabled");
        continue;  // No patching, so skip past sync'ing.
      }
      case CASE_ID_MDS_TAA_MITIGATION: {
        // `nop` out the mitigation if the bug is not present, if we could not
        // mitigate it even if it was, or if we generally want mitigations off.
        const bool present = arch::HasX86MdsTaaBugs(cpuid, msr);
        const bool can_mitigate = arch::CanMitigateX86MdsTaaBugs(cpuid);
        if (!present || !can_mitigate || gBootOptions->x86_disable_spec_mitigations) {
          code_patching::NopFill(insns);
          ktl::string_view qualifier = !present        ? "bug not present"
                                       : !can_mitigate ? "unable to mitigate"
                                                       : "all mitigations disabled";
          PrintCaseInfo(patch, "MDS/TAA bug mitigation disabled (%V)", qualifier);
          break;
        }
        PrintCaseInfo(patch, "MDS/TAA bug mitigation enabled");
        continue;  // No patching, so skip past sync'ing.
      }
      case CASE_ID__X86_COPY_TO_OR_FROM_USER: {
        ktl::string_view name = SelectX86UserCopyAlternative(cpuid);
        auto alternative = GetPatchAlternative(name);
        code_patching::Patch(insns, alternative);
        PrintCaseInfo(patch, "using user-copy alternative \"%V\"", name);
        break;
      }
      case CASE_ID___X86_INDIRECT_THUNK_R11: {
        ktl::string_view name = SelectX86RetpolineAlternative(cpuid, msr, *gBootOptions);
        auto alternative = GetPatchAlternative(name);
        code_patching::Patch(insns, alternative);
        PrintCaseInfo(patch, "using retpoline alternative \"%V\"", name);
        break;
      }
      case CASE_ID___UNSANITIZED_MEMCPY: {
        ktl::string_view name = SelectX86MemcpyAlternative(cpuid);
        auto alternative = GetPatchAlternative(name);
        code_patching::Patch(insns, alternative);
        PrintCaseInfo(patch, "using memcpy alternative \"%V\"", name);
        break;
      }
      case CASE_ID___UNSANITIZED_MEMSET: {
        ktl::string_view name = SelectX86MemsetAlternative(cpuid);
        auto alternative = GetPatchAlternative(name);
        code_patching::Patch(insns, alternative);
        PrintCaseInfo(patch, "using memset alternative \"%V\"", name);
        break;
      }
      default:
        ZX_PANIC("code-patching: unrecognized patch case ID: %u: [%#lx, %#lx)\n", patch.id,
                 patch.range_start, patch.range_start + patch.range_size);
    }
    sync_ctx.SyncRange(patch.range_start, patch.range_size);
  }
}
