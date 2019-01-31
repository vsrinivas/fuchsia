// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>

int main(int argc, char** argv) {
    unsigned char x;
    int fd = 0;
    if (argc == 2) {
        fd = open(argv[1], O_RDONLY);
        if (fd < 0) {
            printf("dump1: cannot open '%s'\n", argv[1]);
            return -1;
        }
    }
    for (;;) {
        int r = read(fd, &x, 1);
        if (r == 0) {
            continue;
        }
        if (r != 1) {
            break;
        }
        if (x == 'z') {
            break;
        }
        printf("%02x ", x);
        fflush(stdout);
    }
    printf("\n");
    return 0;
}
