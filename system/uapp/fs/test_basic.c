// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/stat.h>

#include "misc.h"

int test_basic(void) {
    TRY(mkdir("::alpha", 0755));
    TRY(mkdir("::alpha/bravo", 0755));
    TRY(mkdir("::alpha/bravo/charlie", 0755));
    TRY(mkdir("::alpha/bravo/charlie/delta", 0755));
    TRY(mkdir("::alpha/bravo/charlie/delta/echo", 0755));
    int fd1 = TRY(open("::alpha/bravo/charlie/delta/echo/foxtrot", O_RDWR|O_CREAT, 0644));
    int fd2 = TRY(open("::alpha/bravo/charlie/delta/echo/foxtrot", O_RDWR, 0644));
    TRY(write(fd1, "Hello, World!\n", 14));
    close(fd1);
    close(fd2);
    fd1 = TRY(open("::file.txt", O_CREAT|O_RDWR, 0644));
    close(fd1);
    TRY(unlink("::file.txt"));
    TRY(mkdir("::emptydir", 0755));
    fd1 = TRY(open("::emptydir", O_RDWR, 0644));
    EXPECT_FAIL(unlink("::emptydir"));
    close(fd1);
    TRY(unlink("::emptydir"));
    return 0;
}
