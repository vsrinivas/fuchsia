// Copyright 2016 The Fuchsia Authors
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

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
        printf("mx_cprng_draw had unexpected return: %ld\n", read);
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
            printf("mx_cprng_draw returned an error: %ld\n", read);
            return 1;
        }
        values[byte % BINS]++;
    }

    for (unsigned int i = 0; i < BINS; ++i) {
        printf("bin %u: %llu\n", i, values[i]);
    }

    return 0;
}
