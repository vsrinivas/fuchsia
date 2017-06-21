// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <asm.h>
#include <arch/defines.h>

// Define a special read-only, page-aligned data section called NAME
// anchored with a symbol NAME_image to contain the first SIZE bytes
// (whole pages) of FILENAME.
.macro rodso_image_section name, size
    // We can't use PAGE_SIZE here because on some machines
    // that uses C syntax like 1L instead of plain integers
    // and arithmetic operations that the assembler can handle.
    .if \size % (1 << PAGE_SIZE_SHIFT)
        .error "\name size \size is not multiple of PAGE_SIZE"
    .endif
    .section \name,"a"
    .p2align PAGE_SIZE_SHIFT
.endm

// The whole thing can't be just an assembler macro because a macro
// operand cannot be a string like .incbin needs for the filename.
#define RODSO_IMAGE(name, NAME) \
    rodso_image_section name, NAME##_CODE_END; \
    DATA(name##_image) \
    .incbin NAME##_FILENAME, 0, NAME##_CODE_END; \
    END_DATA(name##_image)
