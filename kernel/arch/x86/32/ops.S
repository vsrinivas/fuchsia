// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2009 Corey Tabaka
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <asm.h>

.text

/* void arch_idle(); */
FUNCTION(arch_idle)
    pushf
    popl %eax
    andl $0x200, %eax
    test %eax, %eax
    je 1f                   /* don't halt if local interrupts are disabled */
    hlt
1:
    ret

