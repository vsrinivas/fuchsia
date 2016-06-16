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

#include <stddef.h>
#include <stdint.h>

/* do a hex dump against stdout 32bits and 8bits at a time */
void hexdump_ex(const void* ptr, size_t len, uint64_t disp_addr);
void hexdump8_ex(const void* ptr, size_t len, uint64_t disp_addr);

static inline void hexdump(const void* ptr, size_t len) {
    hexdump_ex(ptr, len, (uint64_t)((uintptr_t)ptr));
}

static inline void hexdump8(const void* ptr, size_t len) {
    hexdump8_ex(ptr, len, (uint64_t)((uintptr_t)ptr));
}
