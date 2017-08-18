// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <errno.h>
#include <magenta/status.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <threads.h>

static const char* thrd_error_string(int error) {
    switch (error) {
    case thrd_success: return "thrd_success";
    case thrd_busy: return "thrd_busy";
    case thrd_error: return "thrd_error";
    case thrd_nomem: return "thrd_nomem";
    case thrd_timedout: return "thrd_timedout";
    default: return "<unknown thrd status>";
    }
}

int main(int argc, char** argv) {
    for (int idx = 1; idx < argc; idx++) {
        errno = 0;
        long error_long = strtol(argv[idx], NULL, 10);
        if (errno)
            exit(MX_ERR_INVALID_ARGS);
        int error = (int)error_long;
        const char* mx_error = mx_status_get_string((mx_status_t)error);
        char* posix_error = strerror(error);
        const char* thrd_error = thrd_error_string(error);
        printf("Int value: %d\n", error);
        printf("\tMagenta error: %s\n", mx_error);
        printf("\tPosix error: %s\n", posix_error);
        printf("\tC11 thread error: %s\n", thrd_error);
    }
}
