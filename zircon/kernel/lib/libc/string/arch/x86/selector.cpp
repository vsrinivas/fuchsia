// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <arch/x86/feature.h>
#include <assert.h>
#include <lib/code_patching.h>
#include <stddef.h>

extern "C" {

extern void* memcpy(void*, const void*, size_t);
extern void* memcpy_erms(void*, const void*, size_t);
extern void* memcpy_quad(void*, const void*, size_t);

extern void* memset(void*, int, size_t);
extern void* memset_erms(void*, int, size_t);
extern void* memset_quad(void*, int, size_t);

void x86_memcpy_select(const CodePatchInfo* patch) {
    // We are patching a jmp rel8 instruction, which is two bytes.  The rel8
    // value is a signed 8-bit value specifying an offset relative to the
    // address of the next instruction in memory after the jmp instruction.
    const size_t kSize = 2;
    const intptr_t jmp_from_address = reinterpret_cast<intptr_t>(memcpy) + kSize;

    DEBUG_ASSERT(patch->dest_size == kSize);
    DEBUG_ASSERT(reinterpret_cast<uintptr_t>(patch->dest_addr) ==
                 reinterpret_cast<uintptr_t>(memcpy));

    intptr_t offset;
    if (x86_feature_test(X86_FEATURE_ERMS)) {
        offset = reinterpret_cast<intptr_t>(memcpy_erms) - jmp_from_address;
    } else {
        offset = reinterpret_cast<intptr_t>(memcpy_quad) - jmp_from_address;
    }
    DEBUG_ASSERT(offset >= -128 && offset <= 127);
    patch->dest_addr[0] = 0xeb; /* jmp rel8 */
    patch->dest_addr[1] = static_cast<uint8_t>(offset);
}

void x86_memset_select(const CodePatchInfo* patch) {
    // We are patching a jmp rel8 instruction, which is two bytes.  The rel8
    // value is a signed 8-bit value specifying an offset relative to the
    // address of the next instruction in memory after the jmp instruction.
    const size_t kSize = 2;
    const intptr_t jmp_from_address = reinterpret_cast<intptr_t>(memset) + kSize;

    DEBUG_ASSERT(patch->dest_size == kSize);
    DEBUG_ASSERT(reinterpret_cast<uintptr_t>(patch->dest_addr) ==
                 reinterpret_cast<uintptr_t>(memset));

    intptr_t offset;
    if (x86_feature_test(X86_FEATURE_ERMS)) {
        offset = reinterpret_cast<intptr_t>(memset_erms) - jmp_from_address;
    } else {
        offset = reinterpret_cast<intptr_t>(memset_quad) - jmp_from_address;
    }
    DEBUG_ASSERT(offset >= -128 && offset <= 127);
    patch->dest_addr[0] = 0xeb; /* jmp rel8 */
    patch->dest_addr[1] = static_cast<uint8_t>(offset);
}

}
