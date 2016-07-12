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
#include <runtime/process.h>

#include <mxio/debug.h>

static void* arg;

void* __libc_intercept_arg(void* _arg) {
    arg = _arg;
    return NULL;
}

int main(int argc, char** argv) {
    char data[128];
    mx_handle_t h0, h1;
    uint32_t dsz, hsz;
    mx_status_t r;

    cprintf("helper: start\n");
    h0 = (mx_handle_t)(uintptr_t)arg;
    dsz = sizeof(data);
    hsz = 1;
    if ((r = mx_message_read(h0, data, &dsz, &h1, &hsz, 0)) < 0) {
        cprintf("helper: failed to read message %d\n", r);
        return -1;
    }
    if (hsz != 1) {
        cprintf("no handle received\n");
        return -1;
    }
    if ((r = mx_message_write(h1, "okay", 5, &h1, 1, 0)) < 0) {
        cprintf("helper: failed to write message %d\n", r);
        mx_message_write(h1, "fail", 5, NULL, 0, 0);
        return -1;
    }
    printf("helper: done\n");
    return 0;
}
