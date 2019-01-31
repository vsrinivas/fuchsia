// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <zircon/syscalls.h>
#include <zircon/syscalls-ddk.h>

static int val = 5;

void bad_kernel_access_read(void) {
    char cmd[50];
    snprintf(cmd, sizeof(cmd), "db %p 1", &val);
    zx_debug_send_command(cmd, strlen(cmd));
}

void bad_kernel_access_write(void) {
    char cmd[50];
    snprintf(cmd, sizeof(cmd), "mb %p 1 1", &val);
    zx_debug_send_command(cmd, strlen(cmd));
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
