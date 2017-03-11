// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "filesystems.h"

// Print progress every ~5 MB
#define PRINT_SIZE (1 << 19)

bool test_maxfile(void) {
    BEGIN_TEST;
    int fd = open("::bigfile", O_CREAT | O_WRONLY, 0644);
    ASSERT_GT(fd, 0, "");
    char data[8192];
    memset(data, 0xee, sizeof(data));
    ssize_t sz = 0;
    ssize_t r;
    for (;;) {
        if ((r = write(fd, data, sizeof(data))) < 0) {
            fprintf(stderr, "bigfile received error: %s\n", strerror(errno));
            if (errno == EFBIG) {
                fprintf(stderr, "(This was an expected error)\n");
                r = 0;
            }
            break;
        }
        if ((sz + r) % PRINT_SIZE < (sz % PRINT_SIZE)) {
            fprintf(stderr, "wrote %d bytes\n", (int)(sz + r));
        }
        sz += r;
        if (r < (ssize_t)(sizeof(data))) {
            fprintf(stderr, "bigfile write short write of %ld bytes\n", r);
            break;
        }
    }
    ASSERT_EQ(close(fd), 0, "");
    ASSERT_EQ(unlink("::bigfile"), 0, "");
    fprintf(stderr, "wrote %d bytes\n", (int)sz);
    ASSERT_EQ(r, 0, "Saw an unexpected error");
    END_TEST;
}

RUN_FOR_ALL_FILESYSTEMS(maxfile_tests,
    RUN_TEST_LARGE(test_maxfile)
)
