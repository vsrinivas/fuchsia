// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include <magenta/device/block.h>
#include <magenta/device/ramdisk.h>
#include <magenta/syscalls.h>
#include <unittest/unittest.h>

#define RAMCTL_PATH "/dev/misc/ramctl"

int get_ramdisk(const char* name, uint64_t blk_size, uint64_t blk_count) {
    // Open the "ramdisk controller", and ask it to create a ramdisk for us.
    int fd = open(RAMCTL_PATH, O_RDWR);
    ASSERT_GE(fd, 0, "Could not open ramctl device");
    ramdisk_ioctl_config_t config;
    config.blk_size = blk_size;
    config.blk_count = blk_count;
    strcpy(config.name, name);
    ssize_t r = ioctl_ramdisk_config(fd, &config);
    ASSERT_EQ(r, NO_ERROR, "Failed to create ramdisk");
    ASSERT_EQ(close(fd), 0, "Failed to close ramctl");

    // TODO(smklein): This "sleep" prevents a bug from triggering:
    // - 'ioctl_ramdisk_config' --> 'device_add' --> 'open' *should* work, but sometimes
    //   fails, as the ramdisk does not exist in the FS heirarchy yet. (MG-468)
    usleep(10000);

    // At this point, our ramdisk is accessible from filesystem hierarchy
    char ramdisk_path[PATH_MAX];
    snprintf(ramdisk_path, sizeof(ramdisk_path), "%s/%s", RAMCTL_PATH, name);
    fd = open(ramdisk_path, O_RDWR);
    if (fd < 0) {
        printf("OPENING RAMDISK FAILURE. Errno: %d\n", errno);
    }
    ASSERT_GE(fd, 0, "Could not open ramdisk device");

    // Although we are unlinking the ramdisk, it should still be accessible by the file descriptor
    // we opened earlier. Once that fd is closed, the ramdisk should be released.
    ASSERT_GE(ioctl_ramdisk_unlink(fd), 0, "Could not unlink ramdisk device");
    return fd;
}

bool ramdisk_test_simple(void) {
    uint8_t buf[PAGE_SIZE];
    uint8_t out[PAGE_SIZE];

    BEGIN_TEST;
    int fd = get_ramdisk("ramdisk-test-simple", PAGE_SIZE / 2, 512);
    memset(buf, 'a', sizeof(buf));
    memset(out, 0, sizeof(out));

    // Write a page and a half
    ASSERT_EQ(write(fd, buf, sizeof(buf)), (ssize_t) sizeof(buf), "");
    ASSERT_EQ(write(fd, buf, sizeof(buf) / 2), (ssize_t) (sizeof(buf) / 2), "");

    // Seek to the start of the device and read the contents
    ASSERT_EQ(lseek(fd, 0, SEEK_SET), 0, "");
    ASSERT_EQ(read(fd, out, sizeof(out)), (ssize_t) sizeof(out), "");
    ASSERT_EQ(memcmp(out, buf, sizeof(out)), 0, "");

    close(fd);
    END_TEST;
}

bool ramdisk_test_bad_requests(void) {
    uint8_t buf[PAGE_SIZE];

    BEGIN_TEST;
    int fd = get_ramdisk("ramdisk-test-bad-requests", PAGE_SIZE, 512);
    memset(buf, 'a', sizeof(buf));

    // Read / write non-multiples of the block size
    ASSERT_EQ(write(fd, buf, PAGE_SIZE - 1), -1, "");
    ASSERT_EQ(errno, EINVAL, "");
    ASSERT_EQ(write(fd, buf, PAGE_SIZE / 2), -1, "");
    ASSERT_EQ(errno, EINVAL, "");
    ASSERT_EQ(read(fd, buf, PAGE_SIZE - 1), -1, "");
    ASSERT_EQ(errno, EINVAL, "");
    ASSERT_EQ(read(fd, buf, PAGE_SIZE / 2), -1, "");
    ASSERT_EQ(errno, EINVAL, "");

    // Read / write from unaligned offset
    ASSERT_EQ(lseek(fd, 1, SEEK_SET), 1, "");
    ASSERT_EQ(write(fd, buf, PAGE_SIZE), -1, "");
    ASSERT_EQ(errno, EINVAL, "");
    ASSERT_EQ(read(fd, buf, PAGE_SIZE), -1, "");
    ASSERT_EQ(errno, EINVAL, "");

    // Read / write from beyond end of device
    off_t dev_size = PAGE_SIZE * 512;
    ASSERT_EQ(lseek(fd, dev_size, SEEK_SET), dev_size, "");
    ASSERT_EQ(write(fd, buf, PAGE_SIZE), 0, "");
    ASSERT_EQ(read(fd, buf, PAGE_SIZE), 0, "");

    close(fd);
    END_TEST;
}

bool ramdisk_test_multiple(void) {
    uint8_t buf[PAGE_SIZE];
    uint8_t out[PAGE_SIZE];

    BEGIN_TEST;
    int fd1 = get_ramdisk("ramdisk-test-A", PAGE_SIZE, 512);
    int fd2 = get_ramdisk("ramdisk-test-B", PAGE_SIZE, 512);

    // Write 'a' to fd1, write 'b', to fd2
    memset(buf, 'a', sizeof(buf));
    ASSERT_EQ(write(fd1, buf, sizeof(buf)), (ssize_t) sizeof(buf), "");
    memset(buf, 'b', sizeof(buf));
    ASSERT_EQ(write(fd2, buf, sizeof(buf)), (ssize_t) sizeof(buf), "");

    ASSERT_EQ(lseek(fd1, 0, SEEK_SET), 0, "");
    ASSERT_EQ(lseek(fd2, 0, SEEK_SET), 0, "");

    // Read 'b' from fd2, read 'a' from fd1
    ASSERT_EQ(read(fd2, out, sizeof(buf)), (ssize_t) sizeof(buf), "");
    ASSERT_EQ(memcmp(out, buf, sizeof(out)), 0, "");
    close(fd2);

    memset(buf, 'a', sizeof(buf));
    ASSERT_EQ(read(fd1, out, sizeof(buf)), (ssize_t) sizeof(buf), "");
    ASSERT_EQ(memcmp(out, buf, sizeof(out)), 0, "");
    close(fd1);

    END_TEST;
}

BEGIN_TEST_CASE(ramdisk_tests)
RUN_TEST(ramdisk_test_simple)
RUN_TEST(ramdisk_test_bad_requests)
RUN_TEST(ramdisk_test_multiple)
END_TEST_CASE(ramdisk_tests)

int main(int argc, char** argv) {
    return unittest_run_all_tests(argc, argv) ? 0 : -1;
}
