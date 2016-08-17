// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <stdint.h>

#define TRY(func) ({\
    int ret = (func); \
    if (ret < 0) { \
        printf("%s:%d:error: %s -> %d\n", __FILE__, __LINE__, #func, ret); \
        exit(1); \
    } \
    ret; })

int run_fs_tests(void) {
    int fd = TRY(open("(@)", O_RDONLY));

    fd = TRY(open("(@)hello.txt", O_CREAT | O_RDWR, 0644));
    TRY(write(fd, "Hello, World!\n", 14));
    TRY(write(fd, "Hello, World!\n", 14));
    TRY(write(fd, "Hello, World!\n", 14));

    uint8_t* data = malloc(1024*1024);
    for (int i = 0; i < 1024*1024; i++) data[i] = i;
    TRY(write(fd, data, 1024*1024));

    struct stat s;
    TRY(fstat(fd, &s));
    printf("sz = %d\n", (int) s.st_size);

    TRY(mkdir("(@)folder-one", 0755));
    TRY(mkdir("(@)folder-one/folder-two", 0755));
    TRY(mkdir("(@)folder-one/folder-two/folder-three", 0755));
    return 0;
}