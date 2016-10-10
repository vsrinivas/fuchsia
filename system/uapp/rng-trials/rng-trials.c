// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <inttypes.h>
#include <stdio.h>
#include <stdint.h>
#include <magenta/syscalls.h>

#define TRIALS 10000
#define BINS 32

int main(int argc, char** argv) {
    static uint8_t buf[32];
    uint64_t values[BINS] = { 0 };

    mx_ssize_t read = mx_cprng_draw(&buf, sizeof(buf));
    if (read != sizeof(buf)) {
        printf("mx_cprng_draw had unexpected return: %" PRIdPTR "\n", read);
        return 1;
    }
    printf("Drew %zd bytes: ", sizeof(buf));
    for (unsigned int i = 0; i < sizeof(buf); ++i) {
        printf(" %02x", buf[i]);
    }
    printf("\n");

    for (unsigned int i = 0; i < TRIALS; ++i) {
        uint8_t byte;
        read = mx_cprng_draw(&byte, 1);
        if (read != 1) {
            printf("mx_cprng_draw returned an error: %" PRIdPTR "\n", read);
            return 1;
        }
        values[byte % BINS]++;
    }

    for (unsigned int i = 0; i < BINS; ++i) {
        printf("bin %u: %" PRIu64 "\n", i, values[i]);
    }

    return 0;
}
