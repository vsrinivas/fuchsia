// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>

#include <sys/stat.h>
#include <sys/types.h>

#include <mxio/io.h>

#include <lib/md5.h>

#include <magenta/syscalls.h>

int main(int argc, char** argv) {
    if (argc != 2) {
        fprintf(stderr, "invalid arguments\n");
        goto usage;
    }

    if (argv[1][0] == '-')
        goto usage;

    int fd = open(argv[1], O_RDONLY);
    if (fd < 0) {
        fprintf(stderr, "error: cannot open %s for read\n", argv[1]);
        return -1;
    }

    struct stat f_stat;
    if(fstat(fd, &f_stat) < 0) {
        fprintf(stderr, "failed to get file size\n");
        return -1;
    }

    uint8_t* buf = (uint8_t*) malloc(f_stat.st_size);

    if (!buf) {
        fprintf(stderr, "error: failed to allocate %lld bytes\n", f_stat.st_size);
        return -1;
    }

    int r = read(fd, buf, f_stat.st_size);
    if (r < f_stat.st_size) {
        fprintf(stderr, "error: failure %d reading file\n", r);
        return -1;
    }

    uint32_t hash[4] = {0u, 0u, 0u, 0u};

    md5_hash(buf, f_stat.st_size, hash);

    for (int ix = 0; ix != 16; ++ix) {
        printf("%02x", ((uint8_t*)hash)[ix]);
    }

    printf("  (%lld bytes)\n", f_stat.st_size);
    return 0;

usage:
    printf("computes MD5 checksum\n""usage: md5 <file>\n");
    return 1;
}
