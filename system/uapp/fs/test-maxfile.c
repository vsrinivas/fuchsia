// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "misc.h"

int test_maxfile(void) {
    int fd = TRY(open("::bigfile", O_CREAT|O_WRONLY, 0644));
    if (fd < 0) {
        return -1;
    }
    char data[128*1024];
    memset(data, 0xee, sizeof(data));
    ssize_t sz = 0;
    ssize_t r;
    for (;;) {
        if ((r = write(fd, data, sizeof(data))) < 0) {
            // TODO(smklein): ENOSPC? EFBIG?
            if (errno == ENOMEM) {
                r = 0;
            }
            break;
        }
        sz += r;
        if (r < (ssize_t)(sizeof(data))) {
            break;
        }
        fprintf(stderr, "wrote %d bytes\n", (int) sz);
    }
    close(fd);
    unlink("::bigfile");
    fprintf(stderr, "wrote %d bytes\n", (int) sz);
    return (r < 0) ? -1 : 0;
}
