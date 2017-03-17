// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <magenta/new.h>
#include <magenta/syscalls.h>
#include <mxtl/unique_ptr.h>
#include <unittest/unittest.h>

#define MOUNT_POINT "/benchmark"

constexpr size_t kDataSize = (1 << 16);
constexpr size_t kNumOps = 1000;
constexpr uint8_t kMagicByte = 0xee;

// The goal of this benchmark is to get a basic idea of some large read / write
// times for a file.
//
// Caching will no doubt play a part with this benchmark, but it's simple,
// and should give us a rough rule-of-thumb regarding how we're doing.
bool benchmark_write_read(void) {
    BEGIN_TEST;
    printf("\nBenchmarking Write + Read\n");
    int fd = open(MOUNT_POINT "/bigfile", O_CREAT | O_RDWR, 0644);
    ASSERT_GT(fd, 0, "Cannot create file (FS benchmarks assume mounted FS exists at '/benchmark')");

    AllocChecker ac;
    mxtl::unique_ptr<uint8_t[]> data(new (&ac) uint8_t[kDataSize]);
    ASSERT_EQ(ac.check(), true, "");
    memset(data.get(), kMagicByte, kDataSize);

    uint64_t start, end;
    size_t count;
    uint64_t ticks_per_msec = mx_ticks_per_second() / 1000;

    start = mx_ticks_get();
    count = kNumOps;
    while (count--) {
        ASSERT_EQ(write(fd, data.get(), kDataSize), kDataSize, "");
    }
    end = mx_ticks_get();
    printf("Benchmark write: [%10lu] msec\n", (end - start) / ticks_per_msec);

    ASSERT_EQ(lseek(fd, 0, SEEK_SET), 0, "");

    start = mx_ticks_get();
    count = kNumOps;
    while (count--) {
        ASSERT_EQ(read(fd, data.get(), kDataSize), kDataSize, "");
        ASSERT_EQ(data[0], kMagicByte, "");
    }
    end = mx_ticks_get();
    printf("Benchmark read:  [%10lu] msec\n", (end - start) / ticks_per_msec);

    ASSERT_EQ(lseek(fd, 0, SEEK_SET), 0, "");

    start = mx_ticks_get();
    count = kNumOps;
    while (count--) {
        ASSERT_EQ(write(fd, data.get(), kDataSize), kDataSize, "");
    }
    end = mx_ticks_get();
    printf("Benchmark write: [%10lu] msec\n", (end - start) / ticks_per_msec);

    ASSERT_EQ(lseek(fd, 0, SEEK_SET), 0, "");

    start = mx_ticks_get();
    count = kNumOps;
    while (count--) {
        ASSERT_EQ(read(fd, data.get(), kDataSize), kDataSize, "");
        ASSERT_EQ(data[0], kMagicByte, "");
    }
    end = mx_ticks_get();
    printf("Benchmark read:  [%10lu] msec\n", (end - start) / ticks_per_msec);

    ASSERT_EQ(close(fd), 0, "");
    ASSERT_EQ(unlink(MOUNT_POINT "/bigfile"), 0, "");

    END_TEST;
}

#define START_STRING "/aaa"

size_t constexpr cStrlen(const char* str) {
    return *str ? 1 + cStrlen(str + 1) : 0;
}

size_t constexpr kComponentLength = cStrlen(START_STRING);
size_t constexpr kNumComponents = 1000;

template <size_t len>
void increment_str(char* str) {
    // "Increment" the string alphabetically.
    // '/aaa' --> '/aab, '/aaz' --> '/aba', etc
    for (size_t j = len - 1; j > 0; j--) {
        str[j] = static_cast<char>(str[j] + 1);
        if (str[j] > 'z') {
            str[j] = 'a';
        } else {
            return;
        }
    }
}

bool walk_down_path_components(char* path, bool (*cb)(const char* path)) {
    size_t path_len = strlen(path);
    char path_component[kComponentLength + 1];
    strcpy(path_component, START_STRING);

    for (size_t i = 0; i < kNumComponents; i++) {
        strcpy(path + path_len, path_component);
        path_len += kComponentLength;
        ASSERT_TRUE(cb(path), "Callback failure");

        increment_str<kComponentLength>(path_component);
    }
    return true;
}

bool walk_up_path_components(char* path, bool (*cb)(const char* path)) {
    size_t path_len = strlen(path);

    while (path_len != cStrlen(MOUNT_POINT)) {
        ASSERT_TRUE(cb(path), "Callback failure");
        path[path_len - kComponentLength] = 0;
        path_len -= kComponentLength;
    }
    return true;
}

bool mkdir_callback(const char* path) {
    ASSERT_EQ(mkdir(path, 0666), 0, "Could not make directory");
    return true;
}

bool stat_callback(const char* path) {
    struct stat buf;
    ASSERT_EQ(stat(path, &buf), 0, "Could not stat directory");
    return true;
}

bool unlink_callback(const char* path) {
    ASSERT_EQ(unlink(path), 0, "Could not unlink directory");
    return true;
}

bool benchmark_path_walk(void) {
    BEGIN_TEST;
    printf("\nBenchmarking Long path walk\n");
    char path[PATH_MAX];
    strcpy(path, MOUNT_POINT);
    uint64_t start, end;
    uint64_t ticks_per_msec = mx_ticks_per_second() / 1000;

    start = mx_ticks_get();
    ASSERT_TRUE(walk_down_path_components(path, mkdir_callback), "");
    end = mx_ticks_get();
    printf("Benchmark mkdir:  [%10lu] msec\n", (end - start) / ticks_per_msec);

    strcpy(path, MOUNT_POINT);
    start = mx_ticks_get();
    ASSERT_TRUE(walk_down_path_components(path, stat_callback), "");
    end = mx_ticks_get();
    printf("Benchmark stat:   [%10lu] msec\n", (end - start) / ticks_per_msec);

    start = mx_ticks_get();
    ASSERT_TRUE(walk_up_path_components(path, unlink_callback), "");
    end = mx_ticks_get();
    printf("Benchmark unlink: [%10lu] msec\n", (end - start) / ticks_per_msec);
    END_TEST;
}

BEGIN_TEST_CASE(basic_benchmarks)
RUN_TEST_PERFORMANCE(benchmark_write_read)
RUN_TEST_PERFORMANCE(benchmark_path_walk)
END_TEST_CASE(basic_benchmarks)
