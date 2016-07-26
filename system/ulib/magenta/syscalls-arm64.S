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

/* define and implement the magenta syscall wrappers for arm64 */

.text

.cfi_sections .eh_frame, .debug_frame

.macro syscall name, n
.globl \name
.type \name,STT_FUNC
\name:
    .cfi_startproc
    mov    x16, #\n
    svc    #0xf0f /* magic value to differentiate magenta syscalls */
    ret
    .cfi_endproc
.size \name, . - \name
.endm

#if LIBDDK
#define MAGENTA_SYSCALL_DEF(a...)
#define MAGENTA_DDKCALL_DEF(nargs64, nargs32, n, ret, name, args...) syscall mx_##name, n
#else
#define MAGENTA_DDKCALL_DEF(a...)
#define MAGENTA_SYSCALL_DEF(nargs64, nargs32, n, ret, name, args...) syscall mx_##name, n
#endif

#include <magenta/syscalls.inc>
