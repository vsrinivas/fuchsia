// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <errno.h>
#include <magenta/status.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char** argv) {
    for (int idx = 1; idx < argc; idx++) {
        errno = 0;
        long error_long = strtol(argv[idx], NULL, 10);
        if (errno)
            exit(MX_ERR_INVALID_ARGS);
        int error = (int)error_long;
        const char* mx_error = mx_status_get_string((mx_status_t)error);
        char* posix_error = strerror(error);
        printf("Int value: %d\n", error);
        printf("\tMagenta error: %s\n", mx_error);
        printf("\tPosix error: %s\n", posix_error);
    }
}
