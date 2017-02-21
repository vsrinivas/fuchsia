// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <sys/stat.h>
#include <sys/time.h>

#include <magenta/syscalls.h>

#include "filesystems.h"
#include "misc.h"

int64_t nstimespec(struct timespec ts) {
    // assumes very small number of seconds in deltas
    return ts.tv_sec * MX_SEC(1) + ts.tv_nsec;
}

int test_attr(fs_info_t* info) {
    int64_t now = mx_time_get(MX_CLOCK_UTC);

    int fd1 = TRY(open("::file.txt", O_CREAT | O_RDWR, 0644));

    struct timespec ts[2];
    ts[0].tv_nsec = UTIME_OMIT;
    ts[1].tv_sec = (long)(now / MX_SEC(1));
    ts[1].tv_nsec = (long)(now % MX_SEC(1));

    // make sure we get back "now" from stat()
    TRY(futimens(fd1, ts));
    struct stat statb1;
    TRY(fstat(fd1, &statb1));
    assert(statb1.st_mtim.tv_sec == (long)(now / MX_SEC(1)) &&
           statb1.st_mtim.tv_nsec == (long)(now % MX_SEC(1)));
    close(fd1);

    TRY(utimes("::file.txt", NULL));
    struct stat statb2;
    TRY(stat("::file.txt", &statb2));
    assert(nstimespec(statb2.st_mtim) > nstimespec(statb1.st_mtim));

    TRY(unlink("::file.txt"));
    return 0;
}
