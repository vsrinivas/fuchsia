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

#include <stdio.h>
#include <stdlib.h>

#include <magenta/syscalls.h>

#define CHECK(f, expected, message) \
    if ((ret = (f)) != (expected))  \
    printf("Test failed (%s): " #f " returned %d vs. %d\n", message, ret, expected)

int main(void) {
    mx_status_t ret;
    void* unmapped_addr = (void*)4096;
    CHECK(_magenta_debug_write(unmapped_addr, 1), -1, "reading unmapped addr");
    CHECK(_magenta_debug_write((void*)KERNEL_BASE - 1, 5), -1, "read crossing kernel boundary");
    CHECK(_magenta_debug_write((void*)KERNEL_BASE, 1), -1, "read into kernel space");
    CHECK(_magenta_debug_write((void*)&unmapped_addr, sizeof(void*)), (int)sizeof(void*),
          "good read");
    printf("Done\n");
    return 0;
}
