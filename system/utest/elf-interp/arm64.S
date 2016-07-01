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

#include "main-addrs.h"

.macro movabs reg, value
    movz \reg, #(\value & 0xffff)
    movk \reg, #((\value >> 16) & 0xffff), lsl #16
    movk \reg, #((\value >> 32) & 0xffff), lsl #32
    movk \reg, #((\value >> 48) & 0xffff), lsl #48
.endm

.text
.globl _start
.type _start, %function
_start:
    movabs x16, ADDRESS_test_word
    ldr w17, [x16]
    add w17, w17, 1
    str w17, [x16]
    movabs x16, ADDRESS__start
    br x16
