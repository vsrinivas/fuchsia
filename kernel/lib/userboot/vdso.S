// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <asm.h>
#include <arch/defines.h>

// This generated header file defines the *_CODE_END macros that
// indicate the size of the memory image of each built-in ELF file.
#include "code-start.h"

// Define a special read-only, page-aligned data section called NAME
// anchored with a symbol NAME_image to contain the first SIZE bytes
// (whole pages) of FILENAME.
.macro image_section name, size
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
#define FILE_IMAGE(name, NAME) \
    image_section name, NAME##_CODE_END; \
    DATA(name##_image) \
    .incbin NAME##_FILENAME, 0, NAME##_CODE_END; \
    END(name##_image)

// The vDSO image, aka libmagenta.so.
FILE_IMAGE(vdso, VDSO)

// The earliest user-mode process runs this image, aka userboot.
FILE_IMAGE(userboot, USERBOOT)
