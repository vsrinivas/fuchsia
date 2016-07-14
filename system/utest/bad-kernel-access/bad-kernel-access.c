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
#include <string.h>

#include <magenta/syscalls.h>

static int val = 5;

void bad_kernel_access_read(void) {
    char cmd[50];
    snprintf(cmd, sizeof(cmd), "db %p 1", &val);
    mx_debug_send_command(cmd, strlen(cmd));
}

void bad_kernel_access_write(void) {
    char cmd[50];
    snprintf(cmd, sizeof(cmd), "mb %p 1 1", &val);
    mx_debug_send_command(cmd, strlen(cmd));
}

int main(int argc, char **argv) {
    if (argc < 2) {
        printf("Usage: %s [read|write]\n", argv[0]);
        return 1;
    }

    if (!strcmp(argv[1], "read")) {
        bad_kernel_access_read();
    } else if (!strcmp(argv[1], "write")) {
        bad_kernel_access_write();
    }
    return 0;
}
