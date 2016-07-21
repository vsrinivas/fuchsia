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

#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include <magenta/syscalls.h>
#include <unittest/unittest.h>

bool cprng_test_draw_buf_too_large(void) {
    uint8_t buf[MX_CPRNG_DRAW_MAX_LEN + 1];
    BEGIN_TEST;
    mx_ssize_t recvd = mx_cprng_draw(buf, sizeof(buf));
    EXPECT_EQ(recvd, ERR_INVALID_ARGS, "");
    END_TEST;
}

bool cprng_test_draw_bad_buf(void) {
    uint8_t buf[MX_CPRNG_DRAW_MAX_LEN];
    BEGIN_TEST;
    mx_ssize_t recvd = mx_cprng_draw((void*)4, sizeof(buf));
    EXPECT_EQ(recvd, ERR_INVALID_ARGS, "");
    END_TEST;
}

bool cprng_test_draw_success(void) {
    uint8_t buf[MX_CPRNG_DRAW_MAX_LEN] = { 0 };
    BEGIN_TEST;
    mx_ssize_t recvd = mx_cprng_draw(buf, sizeof(buf));
    EXPECT_EQ(recvd, (mx_ssize_t)sizeof(buf), "");

    int num_zeros = 0;
    for (unsigned int i = 0; i < sizeof(buf); ++i) {
        if (buf[i] == 0) {
            num_zeros++;
        }
    }
    // The probability of getting more than 16 zeros if the buf is 256 bytes
    // is 6.76 * 10^-16, so probably not gonna happen.
    EXPECT_LE(num_zeros, 16, "buffer wasn't written to");
    END_TEST;
}

bool cprng_test_add_entropy_bad_buf(void) {
    uint8_t buf[MX_CPRNG_ADD_ENTROPY_MAX_LEN];
    BEGIN_TEST;
    mx_ssize_t recvd = mx_cprng_add_entropy((void*)4, sizeof(buf));
    EXPECT_EQ(recvd, ERR_INVALID_ARGS, "");
    END_TEST;
}

bool cprng_test_add_entropy_buf_too_large(void) {
    uint8_t buf[MX_CPRNG_ADD_ENTROPY_MAX_LEN + 1];
    BEGIN_TEST;
    mx_ssize_t recvd = mx_cprng_add_entropy(buf, sizeof(buf));
    EXPECT_EQ(recvd, ERR_INVALID_ARGS, "");
    END_TEST;
}

BEGIN_TEST_CASE(cprng_tests)
RUN_TEST(cprng_test_draw_buf_too_large)
RUN_TEST(cprng_test_draw_bad_buf)
RUN_TEST(cprng_test_draw_success)
RUN_TEST(cprng_test_add_entropy_buf_too_large)
RUN_TEST(cprng_test_add_entropy_bad_buf)
END_TEST_CASE(cprng_tests)

int main(int argc, char** argv) {
    return unittest_run_all_tests(argc, argv) ? 0 : -1;
}
