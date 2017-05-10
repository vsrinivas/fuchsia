// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "asm.h"

// Call the C _dl_start, which returns a dl_start_return_t containing the
// user entry point and its argument.  Then jump to that entry point with
// the argument in the first argument register (x0, where it was placed by
// the C function's return), clearing the return address and frame pointer
// registers so the user entry point is the base of the call stack.

.hidden _start
ENTRY(_start)
    bl _dl_start
    mov x29, #0 // frame pointer (FP)
    mov x30, #0 // return address (LR)
    br x1
END(_start)
