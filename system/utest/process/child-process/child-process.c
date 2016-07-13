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

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <magenta/syscalls.h>
#include <unittest/test-utils.h>
#include <unittest/unittest.h>

static void* arg;

void* __libc_intercept_arg(void* _arg) {
    arg = _arg;
    return NULL;
}

int main(int argc, char** argv) {
    mx_handle_t handle = (mx_handle_t)(intptr_t)arg;
    unittest_printf("child-process: got arg %u\n", handle);

    if (!tu_wait_readable(handle))
        return -1;

    char buffer[64];
    uint32_t buffer_size = sizeof(buffer);
    memset(buffer, 0, sizeof(buffer));
    tu_message_read(handle, buffer, &buffer_size, NULL, NULL, 0);
    unittest_printf("child-process: received \"%s\"\n", buffer);

    unittest_printf("child-process: sleeping a bit before responding\n");
    mx_nanosleep(200 * 1000 * 1000);

    buffer_size = sizeof(buffer);
    snprintf(buffer, buffer_size, "Hi there to you too!");

    tu_message_write(handle, buffer, strlen(buffer) + 1, NULL, 0, 0);

    unittest_printf("child-process: done\n");

    return 1234;
}
