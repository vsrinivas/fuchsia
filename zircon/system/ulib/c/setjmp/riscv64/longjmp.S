// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "asm.h"
#include "setjmp_impl.h"
#include <zircon/tls.h>

// CFI to find regno at [x16, #8*index].
.macro jb_cfi regno, index
.endm

// CFI to compute regno as ([x0, #8*index] ^ [x16, #8*index]).
.macro jb_cfi_mangled regno, index
.endm

// CFI to compute regno as (mangled ^ mangler).
.macro jb_cfi_mangled_reg regno, mangled, mangler
.endm

.macro restore_pair reg1, reg2, index
.endm

ENTRY(longjmp)
END(longjmp)

ALIAS(longjmp, _longjmp)
WEAK_ALIAS(longjmp, siglongjmp)
