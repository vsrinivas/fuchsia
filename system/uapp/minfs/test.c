// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>

#define TRY(func) ({\
    int ret = (func); \
    if (ret < 0) { \
        printf("%s:%d:error: %s -> %d\n", __FILE__, __LINE__, #func, ret); \
        exit(1); \
    } \
    ret; })

int run_fs_tests(void) {
    int fd = TRY(open("(@)", O_RDONLY));
    printf("fd %d\n", fd);

    fd = TRY(open("(@)hello.txt", O_CREAT | O_RDWR, 0644));
    printf("fd %d\n", fd);

    TRY(mkdir("(@)folder-one", 0755));
    TRY(mkdir("(@)folder-one/folder-two", 0755));
    TRY(mkdir("(@)folder-one/folder-two/folder-three", 0755));
    return 0;
}