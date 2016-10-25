// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>


int main(int argc, char** argv) {

    int verbose = 0;

    if (argc > 1 && strcmp(argv[1], "-v") == 0) {
        verbose++;
        argc--;
        argv++;
    }

    if (argc <= 1) {
        printf("usage: touch [-v] f1 [f2...]\n");
        exit(-1);
    }

    int rc = 0;

    for (int i=1; i<argc; i++) {
        char * fname = argv[i];
        int count;

        printf("touch %s\n", fname);

        // open, maybe create a new file
        int fd = open(fname, O_RDWR|O_CREAT, 0666);
        if (fd < 0) {
            perror("touch open");
            continue;
        }

        // go to the end;
        int seekpos = lseek(fd, 0L, SEEK_END);
        if (seekpos < 0) {
            perror("touch seek");
            close(fd);
            rc = -1;
            continue;
        }

        // extend
        count = write(fd, " ", 1);
        if (count < 1) {
            perror("touch write");
            close(fd);
            rc = -1;
            continue;
        }

        // contract
        ftruncate(fd, seekpos);

        if (verbose) {
            struct stat statb;
            fprintf(stderr, "%s: \n", fname);
            if (fstat(fd, &statb) < 0) {
                perror("fstat");
            } else {
                fprintf(stderr, "create: %ld\n", statb.st_ctime);
                fprintf(stderr, "modify: %ld\n", statb.st_mtime);
            }
        }
        close(fd);
    }

    return rc;
}
