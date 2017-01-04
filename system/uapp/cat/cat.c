// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

static int cat_file(char* name) {
    int in = STDIN_FILENO;
    if (name && strcmp(name, "-")) {
        in = open(name, O_RDONLY);
        if (in == -1) {
            fprintf(stderr, "cat: %s: %s\n", name, strerror(errno));
            return 1;
        }
    }
    char buf[32 * 1024];
    for (;;) {
        ssize_t nread = read(in, buf, sizeof(buf));
        if (nread == 0) {
            if (in != STDIN_FILENO) close(in);
            return 0;
        }
        if (nread == -1) {
            fprintf(stderr, "cat: %s: %s\n", name, strerror(errno));
            return 1;
        }
        char* out = buf;
        while (nread) {
            ssize_t nwrite = write(STDOUT_FILENO, out, nread);
            if (nwrite == -1) {
                fprintf(stderr, "cat: %s: %s\n", name, strerror(errno));
                return 1;
            }
            out += nwrite;
            nread -= nwrite;
        }
    }
}

int main(int argc, char **argv) {
    if (argc == 1) {
        return cat_file(NULL);
    }
    argc--;
    argv++;
    while (argc) {
        int ret = cat_file(argv[0]);
        if (ret != 0) return ret;
        argc--;
        argv++;
    }
    return 0;
}
