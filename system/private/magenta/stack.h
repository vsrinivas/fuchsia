// Copyright 2016 The Fuchsia Authors
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#pragma once

#include <stdint.h>

#define MAGENTA_DEFAULT_STACK_SIZE (256 << 10)

// Given the base and size of the stack block, compute the appropriate
// initial SP value for an initial thread according to the C calling
// convention for the machine.
static inline uintptr_t compute_initial_stack_pointer(uintptr_t base,
                                                      size_t size) {
    // Assume stack grows down.
    uintptr_t sp = base + size;

    // The x86-64 and AArch64 ABIs require 16-byte alignment.
    // The 32-bit ARM ABI only requires 8-byte alignment, but
    // 16-byte alignment is preferable for NEON so use it there too.
    sp &= -16;

#ifdef __x86_64__
    // The x86-64 ABI requires %rsp % 16 = 8 on entry.  The zero word
    // at (%rsp) serves as the return address for the outermost frame.
    sp -= 8;
#elif defined(__arm__) || defined(__aarch64__)
    // The ARMv7 and ARMv8 ABIs both just require that SP be aligned.
#else
# error what machine?
#endif

    return sp;
}
