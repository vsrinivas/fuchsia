// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2009 Corey Tabaka
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <asm.h>
#include <arch/defines.h>

// regular rep stosd version of page zero
FUNCTION(arch_zero_page)
    mov     %edi, %edx
    mov     4(%esp), %edi

    cld
    mov     $PAGE_SIZE >> 2, %ecx
    xor     %eax, %eax

    rep     stosl

    mov     %edx, %edi

    ret
