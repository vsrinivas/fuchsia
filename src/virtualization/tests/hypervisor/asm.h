// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_VIRTUALIZATION_TESTS_HYPERVISOR_ASM_H_
#define SRC_VIRTUALIZATION_TESTS_HYPERVISOR_ASM_H_

// Architecture-independent macros for assembly code.

#ifndef __ASSEMBLER__
#error File should be included only by assembly.
#endif

// clang-format off

// Define new function 'name'.
//
// The function should be terminated by a call to the END_FUNCTION macro:
//
//   FUNCTION foo
//       ...
//   END_FUNCTION
//
// Two symbols '<name>_start' and '<name>_end' are defined surrounding the
// function.
//
// The macro calls the architecture-specific macros 'arch_function_start name
// start end' and 'arch_function_end name start end' just after the start
// symbol and just prior to the end symbol respectively.
.macro FUNCTION name
    .balign 32

    // Define the symbol "${name}_start".
    .global \name\()_start
    .type \name\()_start, STT_FUNC
    \name\()_start:

    // Define the symbol "name".
    .global \name
    .type \name, STT_FUNC
    \name:

    // Perform any architecture-specific set up.
    arch_function_start \name\()_start \name\()_end

    // Define the end of the current function.
    .macro END_FUNCTION
      .purgem END_FUNCTION

      // Perform any architecture-specific tear down.
      arch_function_end \name\()_start \name\()_end

      // Define the symbol "${name}_end".
      .global \name\()_end
      \name\()_end:
    .endm
.endm

#endif  // SRC_VIRTUALIZATION_TESTS_HYPERVISOR_ASM_H_
