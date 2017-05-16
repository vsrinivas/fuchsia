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

#include <magenta/device/vfs.h>
#include <magenta/syscalls.h>
#include <mxalloc/new.h>
#include <mxtl/unique_ptr.h>
#include <unittest/unittest.h>

#define MOUNT_POINT "/benchmark"

constexpr size_t KB = (1 << 10);
constexpr size_t MB = (1 << 20);
constexpr uint8_t kMagicByte = 0xee;

// Return "true" if the fs matches the 'banned' criteria.
template <size_t len>
bool benchmark_banned(int fd, const char (&banned_fs)[len]) {
    char out[len];
    ssize_t r = ioctl_vfs_query_fs(fd, out, sizeof(out));
    if (r != static_cast<ssize_t>(len - 1)) {
        return false;
    }
    return strncmp(banned_fs, out, len - 1) == 0;
}

inline void time_end(const char *str, uint64_t start) {
    uint64_t end = mx_ticks_get();
    uint64_t ticks_per_msec = mx_ticks_per_second() / 1000;
    printf("Benchmark %s: [%10lu] msec\n", str, (end - start) / ticks_per_msec);
}

constexpr int kWriteReadCycles = 3;

// The goal of this benchmark is to get a basic idea of some large read / write
// times for a file.
//
// Caching will no doubt play a part with this benchmark, but it's simple,
// and should give us a rough rule-of-thumb regarding how we're doing.
template <size_t DataSize, size_t NumOps>
bool benchmark_write_read(void) {
    BEGIN_TEST;
    int fd = open(MOUNT_POINT "/bigfile", O_CREAT | O_RDWR, 0644);
    ASSERT_GT(fd, 0, "Cannot create file (FS benchmarks assume mounted FS exists at '/benchmark')");
    const size_t size_mb = (DataSize * NumOps) / MB;
    if (size_mb > 64 && benchmark_banned(fd, "memfs")) {
        return true;
    }
    printf("\nBenchmarking Write + Read (%lu MB)\n", size_mb);

    AllocChecker ac;
    mxtl::unique_ptr<uint8_t[]> data(new (&ac) uint8_t[DataSize]);
    ASSERT_EQ(ac.check(), true, "");
    memset(data.get(), kMagicByte, DataSize);

    uint64_t start;
    size_t count;

    for (int i = 0; i < kWriteReadCycles; i++) {
        char str[100];
        snprintf(str, sizeof(str), "write %d", i);

        start = mx_ticks_get();
        count = NumOps;
        while (count--) {
            ASSERT_EQ(write(fd, data.get(), DataSize), DataSize, "");
        }
        time_end(str, start);

        ASSERT_EQ(lseek(fd, 0, SEEK_SET), 0, "");
        snprintf(str, sizeof(str), "read %d", i);

        start = mx_ticks_get();
        count = NumOps;
        while (count--) {
            ASSERT_EQ(read(fd, data.get(), DataSize), DataSize, "");
            ASSERT_EQ(data[0], kMagicByte, "");
        }
        time_end(str, start);

        ASSERT_EQ(lseek(fd, 0, SEEK_SET), 0, "");
    }

    ASSERT_EQ(close(fd), 0, "");
    ASSERT_EQ(unlink(MOUNT_POINT "/bigfile"), 0, "");

    END_TEST;
}

#define START_STRING "/aaa"

size_t constexpr cStrlen(const char* str) {
    return *str ? 1 + cStrlen(str + 1) : 0;
}

size_t constexpr kComponentLength = cStrlen(START_STRING);

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

template <size_t MaxComponents>
bool walk_down_path_components(char* path, bool (*cb)(const char* path)) {
    static_assert(MaxComponents * kComponentLength + cStrlen(MOUNT_POINT) < PATH_MAX,
                  "Path depth is too long");
    size_t path_len = strlen(path);
    char path_component[kComponentLength + 1];
    strcpy(path_component, START_STRING);

    for (size_t i = 0; i < MaxComponents; i++) {
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

template <size_t MaxComponents>
bool benchmark_path_walk(void) {
    BEGIN_TEST;
    printf("\nBenchmarking Long path walk (%lu components)\n", MaxComponents);
    char path[PATH_MAX];
    strcpy(path, MOUNT_POINT);
    uint64_t start;

    start = mx_ticks_get();
    ASSERT_TRUE(walk_down_path_components<MaxComponents>(path, mkdir_callback), "");
    time_end("mkdir", start);

    strcpy(path, MOUNT_POINT);
    start = mx_ticks_get();
    ASSERT_TRUE(walk_down_path_components<MaxComponents>(path, stat_callback), "");
    time_end("stat", start);

    start = mx_ticks_get();
    ASSERT_TRUE(walk_up_path_components(path, unlink_callback), "");
    time_end("unlink", start);
    END_TEST;
}

BEGIN_TEST_CASE(basic_benchmarks)
RUN_TEST_PERFORMANCE((benchmark_write_read<16 * KB, 1024>))
RUN_TEST_PERFORMANCE((benchmark_write_read<16 * KB, 2048>))
RUN_TEST_PERFORMANCE((benchmark_write_read<16 * KB, 4096>))
RUN_TEST_PERFORMANCE((benchmark_write_read<16 * KB, 8192>))
RUN_TEST_PERFORMANCE((benchmark_write_read<16 * KB, 16384>))
RUN_TEST_PERFORMANCE((benchmark_path_walk<125>))
RUN_TEST_PERFORMANCE((benchmark_path_walk<250>))
RUN_TEST_PERFORMANCE((benchmark_path_walk<500>))
RUN_TEST_PERFORMANCE((benchmark_path_walk<1000>))
END_TEST_CASE(basic_benchmarks)
