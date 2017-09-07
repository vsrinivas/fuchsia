// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <dirent.h>
#include <fcntl.h>
#include <float.h>
#include <limits.h>
#include <math.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

#include <digest/merkle-tree.h>
#include <magenta/device/vfs.h>
#include <magenta/device/rtc.h>
#include <magenta/syscalls.h>
#include <fbl/new.h>
#include <fbl/unique_ptr.h>
#include <fbl/vector.h>
#include <unittest/unittest.h>

#include "blobstore-bench.h"

using digest::Digest;
using digest::MerkleTree;

#define MOUNT_PATH "/blobbench"
#define RESULT_FILE "/tmp/benchmark.csv"
#define END_COUNT 100

#define RUN_FOR_ALL_ORDER(test_type, blob_size, blob_count)          \
   RUN_TEST_PERFORMANCE((test_type<blob_size, blob_count, DEFAULT>)) \
   RUN_TEST_PERFORMANCE((test_type<blob_size, blob_count, REVERSE>)) \
   RUN_TEST_PERFORMANCE((test_type<blob_size, blob_count, RANDOM>))  \
   RUN_TEST_PERFORMANCE((test_type<blob_size, blob_count, FIRST>))   \
   RUN_TEST_PERFORMANCE((test_type<blob_size, blob_count, LAST>))

static char start_time[50];

// Sets start_time to current time reported by rtc
// Returns 0 on success, -1 otherwise
static int GetStartTime() {
    int rtc_fd = open("/dev/misc/rtc", O_RDONLY);
    if (rtc_fd < 0) {
        return -1;
    }

    rtc_t rtc;
    ssize_t n = ioctl_rtc_get(rtc_fd, &rtc);
    if (n < (ssize_t)sizeof(rtc_t)) {
        sprintf(start_time, "???");
        return -1;
    }
    sprintf(start_time,
        "%04d-%02d-%02dT%02d:%02d:%02d",
        rtc.year,
        rtc.month,
        rtc.day,
        rtc.hours,
        rtc.minutes,
        rtc.seconds);
    return 0;
}

// Creates, writes, reads (to verify) and operates on a blob.
// Returns the result of the post-processing 'func' (true == success).
static bool GenerateBlob(fbl::unique_ptr<blob_info_t>* out, size_t blob_size) {
    // Generate a Blob of random data
    fbl::AllocChecker ac;
    fbl::unique_ptr<blob_info_t> info(new (&ac) blob_info_t);
    EXPECT_EQ(ac.check(), true);
    info->data.reset(new (&ac) char[blob_size]);
    EXPECT_EQ(ac.check(), true);
    unsigned int seed = static_cast<unsigned int>(mx_ticks_get());
    for (size_t i = 0; i < blob_size; i++) {
        info->data[i] = (char)rand_r(&seed);
    }
    info->size_data = blob_size;

    // Generate the Merkle Tree
    info->size_merkle = MerkleTree::GetTreeLength(blob_size);
    if (info->size_merkle == 0) {
        info->merkle = nullptr;
    } else {
        info->merkle.reset(new (&ac) char[info->size_merkle]);
        ASSERT_EQ(ac.check(), true);
    }
    Digest digest;
    ASSERT_EQ(MerkleTree::Create(&info->data[0], info->size_data, &info->merkle[0],
                                 info->size_merkle, &digest),
              MX_OK, "Couldn't create Merkle Tree");
    strcpy(info->path, MOUNT_PATH "/");
    size_t prefix_len = strlen(info->path);
    digest.ToString(info->path + prefix_len, sizeof(info->path) - prefix_len);

    // Sanity-check the merkle tree
    ASSERT_EQ(MerkleTree::Verify(&info->data[0], info->size_data, &info->merkle[0],
                                 info->size_merkle, 0, info->size_data, digest),
              MX_OK, "Failed to validate Merkle Tree");

    *out = fbl::move(info);
    return true;
}

// Helper for streaming operations (such as read, write) which may need to be
// repeated multiple times.
template <typename T, typename U>
static inline int StreamAll(T func, int fd, U* buf, size_t max) {
    size_t n = 0;
    while (n != max) {
        ssize_t d = func(fd, &buf[n], max - n);
        if (d < 0) {
            return -1;
        }
        n += d;
    }
    return 0;
}


TestData::TestData(size_t blob_size, size_t blob_count, traversal_order_t order) : blob_size(blob_size), blob_count(blob_count), order(order) {
    indices = new size_t[blob_count];
    samples = new mx_time_t*[NAME_COUNT];
    paths = new char*[blob_count];

    for (int i = 0; i < NAME_COUNT; i++) {
        samples[i] = new mx_time_t[get_max_count()];
    }

    for(size_t i = 0; i < blob_count; i++) {
        paths[i] = new char[PATH_MAX];
    }

    generate_order();
}

TestData::~TestData() {
    for (int i = 0; i < NAME_COUNT; i++) {
        delete[] samples[i];
    }

    for(size_t i = 0; i < blob_count; i++) {
        delete[] paths[i];
    }

    delete[] indices;
    delete[] samples;
    delete[] paths;
}

bool TestData::run_tests() {
    ASSERT_TRUE(create_blobs());
    ASSERT_TRUE(read_blobs());
    ASSERT_TRUE(unlink_blobs());
    return true;
}

void TestData::generate_order() {
    size_t max = blob_count - 1;

    if (order == RANDOM) {
        memset(indices, 0, sizeof(size_t) * blob_count);
        srand(static_cast<unsigned>(mx_ticks_get()));
    }

    while (true) {
        switch(order) {
        case LAST:
        case REVERSE: {
            indices[max] = blob_count - max - 1;
            break;
        }
        case RANDOM: {
            if (max == 0) {
                break;
            }

            size_t index = rand() % max;
            size_t selected = indices[index]; // random number we selected
            size_t swap = indices[max]; // start randomizing at end of array

            if (selected == 0 && index != 0) {
                selected = index; // set value if it has not already been set
            }

            if (swap == 0) {
                swap = max; // set value if it has not already been set
            }

            indices[index] = swap;
            indices[max] = selected;
            break;
        }
        default: {
            indices[max] = max;
            break;
        }
        }

        if (max == 0) {
            break;
        }
        max--;
    }
}

size_t TestData::get_max_count() {
    if (order == FIRST || order == LAST) {
        return END_COUNT;
    }

    return blob_count;
}

void TestData::get_name_str(test_name_t name, char* name_str) {
    switch(name) {
    case CREATE:
        strcpy(name_str, "create");
        break;
    case TRUNCATE:
        strcpy(name_str, "truncate");
        break;
    case WRITE:
        strcpy(name_str, "write");
        break;
    case OPEN:
        strcpy(name_str, "open");
        break;
    case READ:
        strcpy(name_str, "read");
        break;
    case CLOSE:
        strcpy(name_str, "close");
        break;
    case UNLINK:
        strcpy(name_str, "unlink");
        break;
    default:
        strcpy(name_str, "unknown");
        break;
    }
}

void TestData::get_order_str(char* order_str) {
    switch(order) {
    case REVERSE:
        strcpy(order_str, "reverse");
        break;
    case RANDOM:
        strcpy(order_str, "random");
        break;
    case FIRST:
        strcpy(order_str, "first");
        break;
    case LAST:
        strcpy(order_str, "last");
        break;
    default:
        strcpy(order_str, "default");
        break;
    }
}

void TestData::print_order() {
    for (size_t i = 0; i < blob_count; i++) {
        printf("Index %lu: %lu\n", i, indices[i]);
    }
}

inline void TestData::sample_end(mx_time_t start, test_name_t name, size_t index) {
    mx_time_t now = mx_ticks_get();
    samples[name][index] = now - start;
}

bool TestData::report_test(test_name_t name) {
    mx_time_t ticks_per_msec =  mx_ticks_per_second() / 1000;

    double min = DBL_MAX;
    double max = 0;
    double avg = 0;
    double stddev = 0;
    mx_time_t total = 0;

    size_t sample_count = get_max_count();

    double samples_ms[sample_count];

    for (size_t i = 0; i < sample_count; i++) {
        samples_ms[i] = static_cast<double>(samples[name][i]) / static_cast<double>(ticks_per_msec);

        avg += samples_ms[i];
        total += samples[name][i];

        if (samples_ms[i] < min) {
            min = samples_ms[i];
        }

        if (samples_ms[i] > max) {
            max = samples_ms[i];
        }
    }

    avg /= static_cast<double>(sample_count);
    total /= ticks_per_msec;

    for (size_t i = 0; i < sample_count; i++) {
        stddev += pow((static_cast<double>(samples_ms[i]) - avg), 2);
    }

    stddev /= static_cast<double>(sample_count);
    stddev = sqrt(stddev);
    double outlier = avg + (stddev * 3);
    size_t outlier_count = 0;

    for (size_t i = 0; i < sample_count; i++) {
        if (samples_ms[i] > outlier) {
            outlier_count++;
        }
    }

    char test_name[10];
    char test_order[10];
    get_name_str(name, test_name);
    get_order_str(test_order);
    printf("\nBenchmark %10s: [%10lu] msec, average: [%8.2f] msec, min: [%8.2f] msec, max: [%8.2f] msec - %lu outliers (above [%8.2f] msec)",
            test_name, total, avg, min, max, outlier_count, outlier) ;

    FILE* results = fopen(RESULT_FILE, "a");

    ASSERT_NONNULL(results, "Failed to open results file");

    fprintf(results, "%lu,%lu,%s,%s,%s,%f,%f,%f,%f,%f,%lu\n", blob_size, blob_count, start_time, test_name, test_order, avg, min, max, stddev, outlier, outlier_count);
    fclose(results);

    test_name[0] = '\0';
    return true;
}


bool TestData::create_blobs() {
    size_t sample_index = 0;

    for (size_t i = 0; i < blob_count; i++) {
        bool record = (order != FIRST && order != LAST);
        record |= (order == FIRST && i < END_COUNT);
        record |= (order == LAST && i >= blob_count - END_COUNT);

        fbl::unique_ptr<blob_info_t> info;
        ASSERT_TRUE(GenerateBlob(&info, blob_size));
        strcpy(paths[i], info->path);

        // create
        mx_time_t start = mx_ticks_get();
        int fd = open(info->path, O_CREAT | O_RDWR);
        if (record) { sample_end(start, CREATE, sample_index); }

        ASSERT_GT(fd, 0, "Failed to create blob");

        // truncate
        start = mx_ticks_get();
        ASSERT_EQ(ftruncate(fd, blob_size), 0, "Failed to truncate blob");
        if (record) { sample_end(start, TRUNCATE, sample_index); }

        // write
        start = mx_ticks_get();
        ASSERT_EQ(StreamAll(write, fd, info->data.get(), blob_size), 0, "Failed to write Data");
        if (record) { sample_end(start, WRITE, sample_index); }

        ASSERT_EQ(close(fd), 0, "Failed to close blob");

        if (record) {
            sample_index++;
        }
    }

    ASSERT_TRUE(report_test(CREATE));
    ASSERT_TRUE(report_test(TRUNCATE));
    ASSERT_TRUE(report_test(WRITE));

    return true;
}

bool TestData::read_blobs() {
    for (size_t i = 0; i < get_max_count(); i++) {
        size_t index = indices[i];
        const char* path = paths[index];

        // open
        mx_time_t start = mx_ticks_get();
        int fd = open(path, O_RDONLY);
        sample_end(start, OPEN, i);
        ASSERT_GT(fd, 0, "Failed to open blob");

        fbl::AllocChecker ac;
        fbl::unique_ptr<char[]> buf(new (&ac) char[blob_size]);
        EXPECT_EQ(ac.check(), true);
        ASSERT_EQ(lseek(fd, 0, SEEK_SET), 0);

        // read
        start = mx_ticks_get();
        bool success = StreamAll(read, fd, &buf[0], blob_size);
        sample_end(start, READ, i);

        // close
        start = mx_ticks_get();
        ASSERT_EQ(close(fd), 0,  "Failed to close blob");
        sample_end(start, CLOSE, i);

        ASSERT_EQ(success, 0, "Failed to read data");
    }

    ASSERT_TRUE(report_test(OPEN));
    ASSERT_TRUE(report_test(READ));
    ASSERT_TRUE(report_test(CLOSE));
    return true;
}

bool TestData::unlink_blobs() {
    for (size_t i = 0; i < get_max_count(); i++) {
        size_t index = indices[i];
        const char* path = paths[index];

        // unlink
        mx_time_t start = mx_ticks_get();
        ASSERT_EQ(unlink(path), 0, "Failed to unlink");
        sample_end(start, UNLINK, i);
    }

    ASSERT_TRUE(report_test(UNLINK));
    return true;
}

static bool StartBlobstoreBenchmark(size_t blob_size, size_t blob_count, traversal_order_t order) {
    int mountfd = open(MOUNT_PATH, O_RDONLY);
    ASSERT_GT(mountfd, 0, "Failed to open - expected mounted blobstore partition");

    char buf[sizeof(vfs_query_info_t) + MAX_FS_NAME_LEN + 1];
    vfs_query_info_t* info = reinterpret_cast<vfs_query_info_t*>(buf);
    ssize_t r = ioctl_vfs_query_fs(mountfd, info, sizeof(buf) - 1);
    ASSERT_EQ(close(mountfd), 0, "Failed to close mount point");

    ASSERT_GT(r, (ssize_t)sizeof(vfs_query_info_t), "Failed to query fs");
    buf[r] = '\0';
    const char* name = reinterpret_cast<const char*>(buf + sizeof(vfs_query_info_t));
    ASSERT_FALSE(strcmp(name, "blobstore"), "Found non-blobstore partition");
    ASSERT_GT(info->total_bytes - info->used_bytes, blob_size * blob_count, "Not enough free space on disk to run this test");
    ASSERT_GT(info->total_nodes - info->used_nodes, blob_count, "Not enough free space on disk to run this test");

    DIR* dir = opendir(MOUNT_PATH);
    ASSERT_TRUE(readdir(dir) == nullptr, "Expected empty blobstore partition");
    closedir(dir);
    return true;
}

static bool EndBlobstoreBenchmark() {
    DIR* dir = opendir(MOUNT_PATH);
    struct dirent* de;
    ASSERT_NONNULL(dir);

    while ((de = readdir(dir)) != nullptr) {
        char path[PATH_MAX];
        snprintf(path, sizeof(path), "%s/%s", MOUNT_PATH, de->d_name);
        ASSERT_EQ(unlink(path), 0, "Failed to unlink");
    }

    ASSERT_EQ(closedir(dir), 0);
    return true;
}

template <size_t BlobSize, size_t BlobCount, traversal_order_t Order>
static bool benchmark_blob_basic() {
    BEGIN_TEST;
    ASSERT_TRUE(StartBlobstoreBenchmark(BlobSize, BlobCount, Order));
    TestData data(BlobSize, BlobCount, Order);
    bool success = data.run_tests();
    ASSERT_TRUE(EndBlobstoreBenchmark()); //clean up
    ASSERT_TRUE(success);
    END_TEST;
}


BEGIN_TEST_CASE(blobstore_benchmarks)

RUN_FOR_ALL_ORDER(benchmark_blob_basic, 128 * B, 500);
RUN_FOR_ALL_ORDER(benchmark_blob_basic, 128 * B, 1000);
RUN_FOR_ALL_ORDER(benchmark_blob_basic, 128 * B, 10000);

RUN_FOR_ALL_ORDER(benchmark_blob_basic, 512 * B, 500);
RUN_FOR_ALL_ORDER(benchmark_blob_basic, 512 * B, 1000);
RUN_FOR_ALL_ORDER(benchmark_blob_basic, 512 * B, 10000);

RUN_FOR_ALL_ORDER(benchmark_blob_basic, KB, 500);
RUN_FOR_ALL_ORDER(benchmark_blob_basic, KB, 1000);
RUN_FOR_ALL_ORDER(benchmark_blob_basic, KB, 10000);

RUN_FOR_ALL_ORDER(benchmark_blob_basic, 128 * KB, 500);
RUN_FOR_ALL_ORDER(benchmark_blob_basic, 128 * KB, 1000);
RUN_FOR_ALL_ORDER(benchmark_blob_basic, 128 * KB, 10000);

RUN_FOR_ALL_ORDER(benchmark_blob_basic, 512 * KB, 500);
RUN_FOR_ALL_ORDER(benchmark_blob_basic, 512 * KB, 1000);
RUN_FOR_ALL_ORDER(benchmark_blob_basic, 512 * KB, 10000);

RUN_FOR_ALL_ORDER(benchmark_blob_basic, MB, 500);
RUN_FOR_ALL_ORDER(benchmark_blob_basic, MB, 1000);

END_TEST_CASE(blobstore_benchmarks)

int main(int argc, char** argv) {
    if (GetStartTime() != 0) {
        printf("Unable to get start time for test\n");
    }

    return unittest_run_all_tests(argc, argv) ? 0 : -1;
}
