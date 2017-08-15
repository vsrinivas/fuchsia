// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2008-2013 Travis Geiselbrecht
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#ifndef ASSEMBLY
#error for assembly files only
#endif

#include <arch/asm_macros.h>

// for functions that don't have an "end" or don't want .cfi_startproc
#define LOCAL_FUNCTION_LABEL(x) .type x,STT_FUNC; x:
#define FUNCTION_LABEL(x) .global x; LOCAL_FUNCTION_LABEL(x)

#define LOCAL_FUNCTION(x) LOCAL_FUNCTION_LABEL(x) .cfi_startproc
#define FUNCTION(x) .global x; LOCAL_FUNCTION(x)

// for local or global functions
#define END_FUNCTION(x) .cfi_endproc; .size x, . - x

#define LOCAL_DATA(x) .type x,STT_OBJECT; x:
#define DATA(x) .global x; LOCAL_DATA(x)

// for local or global data
#define END_DATA(x) .size x, . - x

// Some DWARF constants for CFI description.
#define DW_CFA_val_expression 0x16
#define DW_OP_lit0            0x30

.macro cfi_register_is_zero reg
    .cfi_escape DW_CFA_val_expression, \reg, 1, DW_OP_lit0
.endm

// We want .debug_frame not .eh_frame.
// WARNING: This is a subtle side-effect of including this file. Heads up!
.cfi_sections .debug_frame

