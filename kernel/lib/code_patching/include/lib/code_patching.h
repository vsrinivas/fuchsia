// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#if defined(ASSEMBLY)

// This is used in assembly code to specify a code fragment that will get
// filled in by the given function, patch_func(), when the kernel starts
// up.  This is used for selecting instructions based on which instructions
// the CPU supports.
#define APPLY_CODE_PATCH_FUNC(patch_func, size_in_bytes)                  \
    0:                                                                    \
    /* Allocate placeholder code.  We fill this with the 1-byte int3 */   \
    /* instruction (0xcc), which will fault if we accidentally execute */ \
    /* it before applying the patch. */                                   \
    .fill size_in_bytes, 1, 0xcc;                                         \
    /* Add "struct CodePatchInfo" entry to the code_patch_table array. */ \
    .pushsection code_patch_table,"a",%progbits;                          \
    .quad patch_func; /* apply_func field */                              \
    .quad 0b; /* dest_addr field */                                       \
    .quad size_in_bytes; /* dest_size field */                            \
    .popsection

#else

#include <stdint.h>

struct CodePatchInfo {
    void (*apply_func)(const CodePatchInfo* patch);
    uint8_t* dest_addr; // Destination code address to patch.
    uint64_t dest_size; // Size of placeholder code.
};

// CODE_TEMPLATE(kVar, "asm...") assembles the given assembly code and
// makes the resulting bytes available in a global variable, kVar.  kVarEnd
// specifies the end address of kVar, allowing the size of the code
// fragment to be calculated.
#define CODE_TEMPLATE(name, asm_code)                              \
    extern const uint8_t name[];                                   \
    extern const uint8_t name##End[];                              \
    __asm__(".pushsection .rodata,\"a\",%progbits\n"               \
            ".global " #name "\n"                                  \
            ".global " #name "End\n"                               \
            #name ":\n"                                            \
            asm_code "\n"                                          \
            #name "End:\n"                                         \
            ".popsection");

void apply_startup_code_patches();

#endif
