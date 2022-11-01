// Copyright 2021 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

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
#include <phys/symbolize.h>

namespace {

// A succeed-or-die wrapper of Patcher::PatchWithAlternative.
void PatchWithAlternative(code_patching::Patcher& patcher, ktl::span<ktl::byte> instructions,
                          ktl::string_view alternative) {
  auto result = patcher.PatchWithAlternative(instructions, alternative);
  if (result.is_error()) {
    printf("%s: code-patching: failed to patch with alternative \"%.*s\": ", ProgramName(),
           static_cast<int>(alternative.size()), alternative.data());
    code_patching::PrintPatcherError(result.error_value());
    abort();
  }
}

void PrintCaseInfo(const code_patching::Directive& patch, const char* fmt, ...) {
  printf("%s: code-patching: ", ProgramName());
  va_list args;
  va_start(args, fmt);
  vprintf(fmt, args);
  va_end(args);
  printf(": [%#lx, %#lx)\n", patch.range_start, patch.range_start + patch.range_size);
}

}  // namespace

// Declared in <lib/code-patching/code-patches.h>.
void ArchPatchCode(code_patching::Patcher patcher, ktl::span<ktl::byte> patchee,
                   uint64_t patchee_load_bias) {
  arch::BootCpuidIo cpuid;
  hwreg::X86MsrIo msr;

  bool performed = false;
  for (const code_patching::Directive& patch : patcher.patches()) {
    ZX_ASSERT(patch.range_start >= patchee_load_bias);
    ZX_ASSERT(patch.range_size <= patchee.size());
    ZX_ASSERT(patchee.size() - patch.range_size >= patch.range_start - patchee_load_bias);

    ktl::span<ktl::byte> insns =
        patchee.subspan(patch.range_start - patchee_load_bias, patch.range_size);

    switch (patch.id) {
      case CASE_ID_SELF_TEST:
        patcher.NopFill(insns);
        PrintCaseInfo(patch, "'smoke test' trap patched");
        performed = true;
        break;
      case CASE_ID_SWAPGS_MITIGATION: {
        // `nop` out the mitigation if the bug is not present, if we could not
        // mitigate it even if it was, or if we generally want mitigations off.
        const bool present = arch::HasX86SwapgsBug(cpuid);
        if (!present || gBootOptions->x86_disable_spec_mitigations) {
          patcher.NopFill(insns);
          ktl::string_view qualifier = !present ? "bug not present" : "all mitigations disabled";
          PrintCaseInfo(patch, "swapgs bug mitigation disabled (%V)", qualifier);
          break;
        }
        PrintCaseInfo(patch, "swapgs bug mitigation enabled");
        break;
      }
      case CASE_ID_MDS_TAA_MITIGATION: {
        // `nop` out the mitigation if the bug is not present, if we could not
        // mitigate it even if it was, or if we generally want mitigations off.
        const bool present = arch::HasX86MdsTaaBugs(cpuid, msr);
        const bool can_mitigate = arch::CanMitigateX86MdsTaaBugs(cpuid);
        if (!present || !can_mitigate || gBootOptions->x86_disable_spec_mitigations) {
          patcher.NopFill(insns);
          ktl::string_view qualifier = !present        ? "bug not present"
                                       : !can_mitigate ? "unable to mitigate"
                                                       : "all mitigations disabled";
          PrintCaseInfo(patch, "MDS/TAA bug mitigation disabled (%V)", qualifier);
          break;
        }
        PrintCaseInfo(patch, "MDS/TAA bug mitigation enabled");
        break;
      }
      case CASE_ID__X86_COPY_TO_OR_FROM_USER: {
        ktl::string_view alternative = SelectX86UserCopyAlternative(cpuid);
        PatchWithAlternative(patcher, insns, alternative);
        PrintCaseInfo(patch, "using user-copy alternative \"%V\"", alternative);
        break;
      }
      case CASE_ID___X86_INDIRECT_THUNK_R11: {
        ktl::string_view alternative = SelectX86RetpolineAlternative(cpuid, msr, *gBootOptions);
        PatchWithAlternative(patcher, insns, alternative);
        PrintCaseInfo(patch, "using retpoline alternative \"%V\"", alternative);
        break;
      }
      case CASE_ID___UNSANITIZED_MEMCPY: {
        ktl::string_view alternative = SelectX86MemcpyAlternative(cpuid);
        PatchWithAlternative(patcher, insns, alternative);
        PrintCaseInfo(patch, "using memcpy alternative \"%V\"", alternative);
        break;
      }
      case CASE_ID___UNSANITIZED_MEMSET: {
        ktl::string_view alternative = SelectX86MemsetAlternative(cpuid);
        PatchWithAlternative(patcher, insns, alternative);
        PrintCaseInfo(patch, "using memset alternative \"%V\"", alternative);
        break;
      }
      default:
        ZX_PANIC("%s: code-patching: unrecognized patch case ID: %u: [%#lx, %#lx)", ProgramName(),
                 patch.id, patch.range_start, patch.range_start + patch.range_size);
    }
  }
  if (!performed) {
    ZX_PANIC("%s: code-patching: failed to patch the kernel", ProgramName());
  }
}
