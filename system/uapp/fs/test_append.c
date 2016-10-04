// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <assert.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

#include "misc.h"

int test_append(void) {
    char buf[4096];
    const char* hello = "Hello, ";
    const char* world = "World!\n";
    struct stat st;

    int fd = TRY(open("::alpha", O_RDWR|O_CREAT, 0644));
    // Write "hello"
    assert(strlen(hello) == strlen(world));
    TRY(write(fd, hello, strlen(hello)));
    TRY(lseek(fd, 0, SEEK_SET));
    TRY(read(fd, buf, strlen(hello)));
    assert(strncmp(buf, hello, strlen(hello)) == 0);

    // At the start of the file, write "world"
    TRY(lseek(fd, 0, SEEK_SET));
    TRY(write(fd, world, strlen(world)));
    TRY(lseek(fd, 0, SEEK_SET));
    TRY(read(fd, buf, strlen(world)));

    // Ensure that the file contains "world", but not "hello"
    assert(strncmp(buf, world, strlen(world)) == 0);
    TRY(stat("::alpha", &st));
    assert(st.st_size == (off_t)strlen(world));
    TRY(unlink("::alpha"));
    TRY(close(fd));

    fd = TRY(open("::alpha", O_RDWR|O_CREAT|O_APPEND, 0644));
    // Write "hello"
    assert(strlen(hello) == strlen(world));
    TRY(write(fd, hello, strlen(hello)));
    TRY(lseek(fd, 0, SEEK_SET));
    TRY(read(fd, buf, strlen(hello)));
    assert(strncmp(buf, hello, strlen(hello)) == 0);

    // At the start of the file, write "world"
    TRY(lseek(fd, 0, SEEK_SET));
    TRY(write(fd, world, strlen(world)));
    TRY(lseek(fd, 0, SEEK_SET));
    TRY(read(fd, buf, strlen(hello) + strlen(world)));

    // Ensure that the file contains both "hello" and "world"
    assert(strncmp(buf, hello, strlen(hello)) == 0);
    assert(strncmp(buf + strlen(hello), world, strlen(world)) == 0);
    TRY(stat("::alpha", &st));
    assert(st.st_size == (off_t)(strlen(hello) + strlen(world)));
    TRY(unlink("::alpha"));
    TRY(close(fd));

    return 0;
}
