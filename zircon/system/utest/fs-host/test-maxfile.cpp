// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <errno.h>

#include <fbl/algorithm.h>

#include "util.h"

#define MB (1 << 20)
#define PRINT_SIZE (MB * 100)

bool test_maxfile(void) {
    BEGIN_TEST;

    int fd = emu_open("::bigfile", O_CREAT | O_RDWR, 0644);
    ASSERT_GT(fd, 0);
    char data_a[8192];
    char data_b[8192];
    char data_c[8192];
    memset(data_a, 0xaa, sizeof(data_a));
    memset(data_b, 0xbb, sizeof(data_b));
    memset(data_c, 0xcc, sizeof(data_c));
    ssize_t sz = 0;
    ssize_t r;

    auto rotate = [&](const char* data) {
        if (data == data_a) {
            return data_b;
        } else if (data == data_b) {
            return data_c;
        } else {
            return data_a;
        }
    };

    const char* data = data_a;
    for (;;) {
        if ((r = emu_write(fd, data, sizeof(data_a))) < 0) {
            fprintf(stderr, "bigfile received error: %s\n", strerror(errno));
            if ((errno == EFBIG) || (errno == ENOSPC)) {
                // Either the file should be too big (EFBIG) or the file should
                // consume the whole volume (ENOSPC).
                fprintf(stderr, "(This was an expected error)\n");
                r = 0;
            }
            break;
        }
        if ((sz + r) % PRINT_SIZE < (sz % PRINT_SIZE)) {
            fprintf(stderr, "wrote %zu MB\n", static_cast<size_t>((sz + r) / MB));
        }
        sz += r;
        if (r < (ssize_t)(sizeof(data_a))) {
            fprintf(stderr, "bigfile write short write of %ld bytes\n", r);
            break;
        }

        // Rotate which data buffer we use
        data = rotate(data);
    }
    ASSERT_EQ(r, 0, "Saw an unexpected error from write");

    struct stat buf;
    ASSERT_EQ(emu_fstat(fd, &buf), 0, "Couldn't stat max file");
    ASSERT_EQ(buf.st_size, sz, "Unexpected max file size");

    // Try closing, re-opening, and verifying the file
    ASSERT_EQ(emu_close(fd), 0);
    fd = emu_open("::bigfile", O_RDWR, 0644);
    char readbuf[8192];
    ssize_t bytes_read = 0;
    data = data_a;
    while (bytes_read < sz) {
        r = emu_read(fd, readbuf, sizeof(readbuf));
        ASSERT_EQ(r, fbl::min(sz - bytes_read, static_cast<ssize_t>(sizeof(readbuf))));
        ASSERT_EQ(memcmp(readbuf, data, r), 0, "File failed to verify");
        data = rotate(data);
        bytes_read += r;
    }

    ASSERT_EQ(bytes_read, sz);

    ASSERT_EQ(emu_close(fd), 0);
    ASSERT_EQ(run_fsck(), 0);
    END_TEST;
}

RUN_MINFS_TESTS(maxfile_tests,
    RUN_TEST_LARGE(test_maxfile)
)
