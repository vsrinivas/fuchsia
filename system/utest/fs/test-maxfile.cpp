// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

#include <fbl/algorithm.h>

#include "filesystems.h"
#include "misc.h"

#define MB (1 << 20)
#define PRINT_SIZE (MB * 100)

enum MountType {
    DoRemount,
    DontRemount,
};

// Test writing as much as we can to a file until we run
// out of space
template <MountType mt>
bool test_maxfile(void) {
    BEGIN_TEST;

    if (!test_info->can_be_mounted && mt == DoRemount) {
        fprintf(stderr, "Filesystem cannot be mounted; cannot remount\n");
        return true;
    }

    int fd = open("::bigfile", O_CREAT | O_RDWR, 0644);
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
        if ((r = write(fd, data, sizeof(data_a))) < 0) {
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
            fprintf(stderr, "wrote %lu MB\n", static_cast<size_t>((sz + r) / MB));
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
    fprintf(stderr, "wrote %lu bytes\n", static_cast<size_t>(sz));

    struct stat buf;
    ASSERT_EQ(fstat(fd, &buf), 0, "Couldn't stat max file");
    ASSERT_EQ(buf.st_size, sz, "Unexpected max file size");

    // Try closing, re-opening, and verifying the file
    ASSERT_EQ(close(fd), 0);
    if (mt == DoRemount) {
        ASSERT_TRUE(check_remount(), "Could not remount filesystem");
    }
    fd = open("::bigfile", O_RDWR, 0644);
    ASSERT_GT(fd, 0);
    ASSERT_EQ(fstat(fd, &buf), 0, "Couldn't stat max file");
    ASSERT_EQ(buf.st_size, sz, "Unexpected max file size");
    char readbuf[8192];
    ssize_t bytes_read = 0;
    data = data_a;
    while (bytes_read < sz) {
        r = read(fd, readbuf, sizeof(readbuf));
        ASSERT_EQ(r, fbl::min(sz - bytes_read, static_cast<ssize_t>(sizeof(readbuf))));
        ASSERT_EQ(memcmp(readbuf, data, r), 0, "File failed to verify");
        data = rotate(data);
        bytes_read += r;
    }

    ASSERT_EQ(bytes_read, sz);

    ASSERT_EQ(unlink("::bigfile"), 0);
    ASSERT_EQ(close(fd), 0);
    END_TEST;
}

// Test writing to two files, in alternation, until we run out
// of space. For trivial (sequential) block allocation policies,
// this will create two large files with non-contiguous block
// allocations.
template <MountType mt>
bool test_zipped_maxfiles(void) {
    BEGIN_TEST;

    if (!test_info->can_be_mounted && mt == DoRemount) {
        fprintf(stderr, "Filesystem cannot be mounted; cannot remount\n");
        return true;
    }

    int fda = open("::bigfile-A", O_CREAT | O_RDWR, 0644);
    int fdb = open("::bigfile-B", O_CREAT | O_RDWR, 0644);
    ASSERT_GT(fda, 0);
    ASSERT_GT(fdb, 0);
    char data_a[8192];
    char data_b[8192];
    memset(data_a, 0xaa, sizeof(data_a));
    memset(data_b, 0xbb, sizeof(data_b));
    ssize_t sz_a = 0;
    ssize_t sz_b = 0;
    ssize_t r;

    ssize_t* sz = &sz_a;
    int fd = fda;
    const char* data = data_a;
    for (;;) {
        if ((r = write(fd, data, sizeof(data_a))) <= 0) {
            fprintf(stderr, "bigfile received error: %s\n", strerror(errno));
            // Either the file should be too big (EFBIG) or the file should
            // consume the whole volume (ENOSPC).
            ASSERT_TRUE(errno == EFBIG || errno == ENOSPC);
            fprintf(stderr, "(This was an expected error)\n");
            break;
        }
        if ((*sz + r) % PRINT_SIZE < (*sz % PRINT_SIZE)) {
            fprintf(stderr, "wrote %lu MB\n", static_cast<size_t>((*sz + r) / MB));
        }
        *sz += r;

        fd = (fd == fda) ? fdb : fda;
        data = (data == data_a) ? data_b : data_a;
        sz = (sz == &sz_a) ? &sz_b : &sz_a;
    }
    fprintf(stderr, "wrote %lu bytes (to A)\n", static_cast<size_t>(sz_a));
    fprintf(stderr, "wrote %lu bytes (to B)\n", static_cast<size_t>(sz_b));

    struct stat buf;
    ASSERT_EQ(fstat(fda, &buf), 0, "Couldn't stat max file");
    ASSERT_EQ(buf.st_size, sz_a, "Unexpected max file size");
    ASSERT_EQ(fstat(fdb, &buf), 0, "Couldn't stat max file");
    ASSERT_EQ(buf.st_size, sz_b, "Unexpected max file size");

    // Try closing, re-opening, and verifying the file
    ASSERT_EQ(close(fda), 0);
    ASSERT_EQ(close(fdb), 0);
    if (mt == DoRemount) {
        ASSERT_TRUE(check_remount(), "Could not remount filesystem");
    }
    fda = open("::bigfile-A", O_RDWR, 0644);
    fdb = open("::bigfile-B", O_RDWR, 0644);
    ASSERT_GT(fda, 0);
    ASSERT_GT(fdb, 0);

    char readbuf[8192];
    ssize_t bytes_read_a = 0;
    ssize_t bytes_read_b = 0;

    fd = fda;
    data = data_a;
    sz = &sz_a;
    ssize_t* bytes_read = &bytes_read_a;
    while (*bytes_read < *sz) {
        r = read(fd, readbuf, sizeof(readbuf));
        ASSERT_EQ(r, fbl::min(*sz - *bytes_read, static_cast<ssize_t>(sizeof(readbuf))));
        ASSERT_EQ(memcmp(readbuf, data, r), 0, "File failed to verify");
        *bytes_read += r;

        fd = (fd == fda) ? fdb : fda;
        data = (data == data_a) ? data_b : data_a;
        sz = (sz == &sz_a) ? &sz_b : &sz_a;
        bytes_read = (bytes_read == &bytes_read_a) ? &bytes_read_b : &bytes_read_a;
    }

    ASSERT_EQ(bytes_read_a, sz_a);
    ASSERT_EQ(bytes_read_b, sz_b);

    ASSERT_EQ(unlink("::bigfile-A"), 0);
    ASSERT_EQ(unlink("::bigfile-B"), 0);
    ASSERT_EQ(close(fda), 0);
    ASSERT_EQ(close(fdb), 0);

    END_TEST;
}

RUN_FOR_ALL_FILESYSTEMS_SIZE(maxfile_tests, (1 << 29),
    RUN_TEST_LARGE((test_maxfile<DontRemount>))
    RUN_TEST_LARGE((test_maxfile<DoRemount>))
    RUN_TEST_LARGE((test_zipped_maxfiles<DontRemount>))
    RUN_TEST_LARGE((test_zipped_maxfiles<DoRemount>))
)
