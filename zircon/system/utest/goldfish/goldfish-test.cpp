// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <unistd.h>
#include <unittest/unittest.h>

static bool GoldfishPipeTest() {
    BEGIN_TEST;

    int fd = open("/dev/class/goldfish-pipe/000", O_RDWR);
    EXPECT_GE(fd, 0);

    // Connect to pingpong service.
    constexpr char kPipeName[] = "pipe:pingpong";
    ssize_t bytes = strlen(kPipeName) + 1;
    EXPECT_EQ(write(fd, kPipeName, bytes), bytes);

    // Write 1 byte.
    const uint8_t kSentinel = 0xaa;
    EXPECT_EQ(write(fd, &kSentinel, 1), 1);

    // Read 1 byte result.
    uint8_t result = 0;
    EXPECT_EQ(read(fd, &result, 1), 1);

    // pingpong service should have returned the data received.
    EXPECT_EQ(result, kSentinel);

    // Write 3 * 4096 bytes.
    const size_t kSize = 3 * 4096;
    uint8_t send_buffer[kSize];
    memset(send_buffer, kSentinel, kSize);
    EXPECT_EQ(write(fd, send_buffer, kSize), kSize);

    // Read 3 * 4096 bytes.
    uint8_t recv_buffer[kSize];
    EXPECT_EQ(read(fd, recv_buffer, kSize), kSize);

    // pingpong service should have returned the data received.
    EXPECT_EQ(memcmp(send_buffer, recv_buffer, kSize), 0);

    close(fd);

    END_TEST;
}

BEGIN_TEST_CASE(GoldfishPipeTests)
RUN_TEST(GoldfishPipeTest)
END_TEST_CASE(GoldfishPipeTests)

int main(int argc, char** argv) {
    if (access("/dev/sys/platform/acpi/goldfish", F_OK) != -1) {
        return unittest_run_all_tests(argc, argv) ? 0 : -1;
    }
    return 0;
}
