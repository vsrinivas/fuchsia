// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

.macro zircon_syscall num, name, caller
    li t0, \num
    ecall
// This symbol at the return address identifies this as an approved call site.
    .hidden CODE_SYSRET_\name\()_VIA_\caller
CODE_SYSRET_\name\()_VIA_\caller\():
.endm

// CFI aware push and pop macros.
// SP must always be aligned to 16, so never push just one register.
.macro push_regpair reg0, reg1
.endm
.macro pop_regpair reg0, reg1
.endm
