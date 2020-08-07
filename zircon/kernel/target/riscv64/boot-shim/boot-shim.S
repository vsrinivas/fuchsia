// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <asm.h>

#define STACK_SIZE  4096

.section .text.boot0,"ax"
FUNCTION(_start)
    # Setup stack
    la    sp, stack_end

    # Retrieve kernel entry point. The boot hart id is already in a0.
    jal   boot_shim

    # Jump into the kernel
    jalr  a1
END_FUNCTION(_start)

.bss
.balign 16
LOCAL_DATA(stack)
    .skip STACK_SIZE
LOCAL_DATA(stack_end)
END_DATA(stack)
