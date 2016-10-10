// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>

#include <sys/stat.h>

#include <mxio/io.h>

#include <lib/crypto/cryptolib.h>

#include <magenta/syscalls.h>

uint8_t buf[32 * 1024];

int usage(int argc, char** argv) {
    fprintf(stderr,
            "computes SHA256 checksum\n"
            "usage: %s -h                Display this message\n"
            "       %s FILE...           Hash the given files\n",
            argv[0], argv[0]);
    return 1;
}

int main(int argc, char** argv) {
    if (argc < 2) {
        fprintf(stderr, "error: invalid arguments\n");
        return usage(argc, argv);
    }

    if (argv[1][0] == '-')
        return usage(argc, argv);

    for (int i = 1; i < argc; ++i) {
        int fd = open(argv[i], O_RDONLY);
        if (fd < 0) {
            fprintf(stderr, "error: cannot open %s for read\n", argv[i]);
            return 1;
        }

        clSHA256_CTX ctx;
        clSHA256_init(&ctx);

        while (1) {
            int r = read(fd, buf, sizeof(buf));
            if (r < 0) {
                fprintf(stderr, "error: failure %d reading file\n", r);
                return 1;
            }
            if (r == 0) {
                break;
            }

            clHASH_update(&ctx, buf, r);
        }

        close(fd);

        const uint8_t* hash = clHASH_final(&ctx);

        for (int ix = 0; ix != clSHA256_DIGEST_SIZE; ++ix) {
            printf("%02x", ((uint8_t*)hash)[ix]);
        }

        printf("  %s\n", argv[i]);
    }
    return 0;
}
