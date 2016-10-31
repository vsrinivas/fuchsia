// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <utime.h>


int main(int argc, char** argv) {

    bool verbose = false;
    bool use_null_default = false;
    bool write_data = false;
    bool trunc_data = false;
    bool trunc_append = false;

    while (argc > 1 && argv[1][0] == '-') {
        if (strcmp(argv[1], "-v") == 0) {
            verbose = true;
        } else if (strcmp(argv[1], "-T") == 0) {
            trunc_append = true;
        } else if (strcmp(argv[1], "-d") == 0) {
            use_null_default = true;
        } else if (strcmp(argv[1], "-w") == 0) {
            // write some data, make sure that updates
            write_data = true;
        } else if (strcmp(argv[1], "-t") == 0) {
            // truncate, make sure that updates
            trunc_data = true;
        } else {
            break;
        }

        argc--;
        argv++;
    }

    if (argc <= 1 || argv[1][0] == '-') {
        printf("usage: touch [-w] [-t] [-T] [-d] [-v] f1 [f2...]\n");
        exit(-1);
    }

    int rc = 0;

    for (int i=1; i<argc; i++) {
        char * fname = argv[i];

        printf("touch %s\n", fname);

        // open, maybe create a new file
        int fd = open(fname, O_RDWR|O_CREAT, 0666);
        if (fd < 0) {
            perror("touch open");
            continue;
        }

        if (trunc_append) {
            // indirectly update change time by
            // making a change to file contents
            // (which you then reverse

            // append a byte
            int seekpos = lseek(fd, 0L, SEEK_END);
            if (seekpos < 0) {
                perror("touch seek");
                close(fd);
                rc = -1;
                continue;
            }

            int count = write(fd, " ", 1);
            if (count < 1) {
                perror("touch write");
                close(fd);
                rc = -1;
                continue;
            }

            // get rid of the extra byte
            int err = ftruncate(fd, seekpos);
            if (err < 0) {
                perror("touch ftruncate");
                rc = -1;
            }
        } else if (write_data) {
            // write 1024 bytes of zero
            char buf[1024] = {};
            int len = write(fd, buf, sizeof(buf));
            if (len < 0) {
                perror("touch write data");
                rc = -1;
            }
        } else if (trunc_data) {
            int err = ftruncate(fd, 0L);
            if (err < 0) {
                perror("touch trunc data ftruncate");
                rc = -1;
            }
        } else {
            // default: set the time directly via utime

            // access time not currently implemented
            if (use_null_default) {
                int err = utime(fname, NULL);
                if (err < 0) {
                    perror("touch utime default");
                    rc = -1;
                }
            } else {
                struct utimbuf ut;

                ut.modtime = 1234567;
                int err = utime(fname, &ut);
                if (err < 0) {
                    perror("touch utime");
                    rc = -1;
                }
            }
        }

        if (verbose) {
            struct stat statb;
            fprintf(stderr, "%s: \n", fname);
            if (fstat(fd, &statb) < 0) {
                perror("fstat");
            } else {
                fprintf(stderr, "create: %#lx(%ld)\n", statb.st_ctime, statb.st_ctime);
                fprintf(stderr, "modify: %#lx(%ld)\n", statb.st_mtime, statb.st_mtime);
            }
        }
        close(fd);
    }

    return rc;
}
