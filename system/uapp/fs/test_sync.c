// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/stat.h>

#include "misc.h"

// TODO(smklein): Create a more complex test, capable of mocking a block device
// and ensuring that data is actually being flushed to a block device.
// For now, test that 'fsync' and 'fdatasync' don't throw errors for file and
// directories.
int test_sync(void) {
    int fd = TRY(open("::alpha", O_RDWR|O_CREAT|O_EXCL, 0644));
    TRY(write(fd, "Hello, World!\n", 14));
    TRY(fsync(fd));
    TRY(lseek(fd, 0, SEEK_SET));
    TRY(write(fd, "Adios, World!\n", 14));
    TRY(fdatasync(fd));
    close(fd);
    TRY(unlink("::alpha"));

    TRY(mkdir("::dirname", 0755));
    fd = TRY(open("::dirname", O_RDWR, 0644));
    TRY(fsync(fd));
    TRY(fdatasync(fd));
    close(fd);
    TRY(unlink("::dirname"));
    return 0;
}

