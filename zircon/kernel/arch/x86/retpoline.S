// Copyright 2019 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <asm.h>
#include <lib/code_patching.h>

// "Unsafe" trampolines, used when speculative execution mitigations are disabled.
FUNCTION(__x86_indirect_thunk_unsafe_r11)
  jmp *%r11
.global __x86_indirect_thunk_unsafe_r11_end
__x86_indirect_thunk_unsafe_r11_end:
END_FUNCTION(__x86_indirect_thunk_unsafe_r11)

// AMD-specific trampolines
// "Software Techniques for Managing Speculation on AMD Processors", Mitigation V2-2
FUNCTION(__x86_indirect_thunk_amd_r11)
  lfence
  jmp *%r11
.global __x86_indirect_thunk_amd_r11_end
__x86_indirect_thunk_amd_r11_end:
END_FUNCTION(__x86_indirect_thunk_amd_r11)

// Generic x86 'retpoline' implementation
FUNCTION(__x86_indirect_thunk_r11)
  call .LINT1
.LINT0:
  pause
  lfence
  jmp .LINT0
.LINT1:
  movq %r11, (%rsp)
  ret
  APPLY_CODE_PATCH_FUNC_WITH_DEFAULT(x86_retpoline_select, __x86_indirect_thunk_r11, 17)
END_FUNCTION(__x86_indirect_thunk_r11)
