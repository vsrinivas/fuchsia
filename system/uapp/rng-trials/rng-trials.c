// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <inttypes.h>
#include <stdio.h>
#include <stdint.h>
#include <zircon/status.h>
#include <zircon/syscalls.h>

#define TRIALS 10000
#define BINS 32

int main(int argc, char** argv) {
    static uint8_t buf[32];
    uint64_t values[BINS] = { 0 };

    zx_cprng_draw(&buf, sizeof(buf));

    printf("Drew %zd bytes: ", sizeof(buf));
    for (unsigned int i = 0; i < sizeof(buf); ++i) {
        printf(" %02x", buf[i]);
    }
    printf("\n");

    for (unsigned int i = 0; i < TRIALS; ++i) {
        uint8_t byte;
        zx_cprng_draw(&byte, 1);
        values[byte % BINS]++;
    }

    for (unsigned int i = 0; i < BINS; ++i) {
        printf("bin %u: %" PRIu64 "\n", i, values[i]);
    }

    return 0;
}
