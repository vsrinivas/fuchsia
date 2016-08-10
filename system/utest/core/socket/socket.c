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

#include <assert.h>
#include <magenta/syscalls.h>
#include <unittest/unittest.h>
#include <stdbool.h>
#include <stdio.h>
#include <unistd.h>

static bool socket_basic(void) {
    BEGIN_TEST;

    mx_status_t status;
    mx_ssize_t ssize;

    mx_handle_t h[2];
    status = mx_socket_create(h, 0);
    ASSERT_EQ(status, NO_ERROR, "error in create");

    static const uint32_t write_data[] = { 0xdeadbeef, 0xc0ffee };
    ssize = mx_socket_write(h[0], 0u, sizeof(write_data[0]), &write_data[0]);
    ASSERT_EQ(ssize, (int)sizeof(write_data[0]), "error in write");
    ssize = mx_socket_write(h[0], 0u, sizeof(write_data[1]), &write_data[1]);
    ASSERT_EQ(ssize, (int)sizeof(write_data[1]), "error in write");

    uint32_t read_data[] = { 0, 0 };
    ssize = mx_socket_read(h[1], 0u, sizeof(read_data), read_data);
    ASSERT_EQ(ssize, (int)sizeof(read_data), "error in read");
    ASSERT_EQ(read_data[0], write_data[0], "");
    ASSERT_EQ(read_data[1], write_data[1], "");

    mx_handle_close(h[0]);
    mx_handle_close(h[1]);
    END_TEST;
}

BEGIN_TEST_CASE(socket_tests)
RUN_TEST(socket_basic)
END_TEST_CASE(socket_tests)

#ifndef BUILD_COMBINED_TESTS
int main(int argc, char** argv) {
    return unittest_run_all_tests(argc, argv) ? 0 : -1;
}
#endif
