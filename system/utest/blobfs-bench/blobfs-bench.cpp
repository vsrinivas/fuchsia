// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include "blobfs-bench.h"

#include <dirent.h>
#include <fcntl.h>
#include <float.h>
#include <math.h>
#include <sys/stat.h>
#include <unistd.h>

#include <digest/digest.h>
#include <digest/merkle-tree.h>
#include <fbl/new.h>
#include <fbl/unique_ptr.h>
#include <fbl/vector.h>
#include <unittest/unittest.h>
#include <zircon/device/rtc.h>
#include <zircon/device/vfs.h>
#include <zircon/syscalls.h>

using digest::Digest;
using digest::MerkleTree;

#define RUN_FOR_ALL_ORDER(test_type, blob_size, blob_count)           \
    RUN_TEST_PERFORMANCE(                                             \
        (test_type<blob_size, blob_count, TraversalOrder::kDefault>)) \
    RUN_TEST_PERFORMANCE(                                             \
        (test_type<blob_size, blob_count, TraversalOrder::kReverse>)) \
    RUN_TEST_PERFORMANCE(                                             \
        (test_type<blob_size, blob_count, TraversalOrder::kRandom>))  \
    RUN_TEST_PERFORMANCE(                                             \
        (test_type<blob_size, blob_count, TraversalOrder::kFirst>))   \
    RUN_TEST_PERFORMANCE(                                             \
        (test_type<blob_size, blob_count, TraversalOrder::kLast>))

namespace {

// Maximum number of paths_ to look up when TraversalOrder is LAST or FIRST.
constexpr size_t kEndCount = 100;

// Maximum length for test name.
constexpr size_t kTestNameMaxLength = 20;

// Maximum length for test order_.
constexpr size_t kTestOrderMaxLength = 10;

// Path to mounted Blobfs File System.
constexpr char kMountPath[] = "/tmp/blobbench";

// Output file path.
constexpr char kOutputPath[] = "/tmp/benchmark.csv";

// Shortcut to for TestName::kCount
constexpr int kNameCount = static_cast<int>(TestName::kCount);

// Byte conversions
constexpr size_t kKb = (1 << 10);
constexpr size_t kMb = (1 << 20);

static char start_time[50];

bool StartBlobfsBenchmark(size_t blob_size, size_t blob_count,
                                 TraversalOrder order_) {
    int mountfd = open(kMountPath, O_RDONLY);
    ASSERT_GT(
        mountfd, 0,
        "Failed to open - expected mounted blobfs partition at /tmp/blobbench");

    char buf[sizeof(vfs_query_info_t) + MAX_FS_NAME_LEN + 1];
    vfs_query_info_t* info = reinterpret_cast<vfs_query_info_t*>(buf);
    ssize_t r = ioctl_vfs_query_fs(mountfd, info, sizeof(buf) - 1);
    ASSERT_EQ(close(mountfd), 0, "Failed to close mount point");

    ASSERT_GT(r, (ssize_t)sizeof(vfs_query_info_t), "Failed to query fs");
    buf[r] = '\0';
    const char* name =
        reinterpret_cast<const char*>(buf + sizeof(vfs_query_info_t));
    ASSERT_FALSE(strcmp(name, "blobfs"), "Found non-blobfs partition");
    ASSERT_GT(info->total_bytes - info->used_bytes, blob_size * blob_count,
              "Not enough free space on disk to run this test");
    ASSERT_GT(info->total_nodes - info->used_nodes, blob_count,
              "Not enough free space on disk to run this test");

    DIR* dir = opendir(kMountPath);
    ASSERT_TRUE(readdir(dir) == nullptr, "Expected empty blobfs partition");
    closedir(dir);
    return true;
}

bool EndBlobfsBenchmark() {
    DIR* dir = opendir(kMountPath);
    struct dirent* de;
    ASSERT_NONNULL(dir);

    while ((de = readdir(dir)) != nullptr) {
        char path[PATH_MAX];
        snprintf(path, sizeof(path), "%s/%s", kMountPath, de->d_name);
        ASSERT_EQ(unlink(path), 0, "Failed to unlink");
    }

    ASSERT_EQ(closedir(dir), 0);
    return true;
}

template <size_t BlobSize, size_t BlobCount, TraversalOrder Order>
bool RunBasicBlobBenchmark() {
    BEGIN_TEST;
    ASSERT_TRUE(StartBlobfsBenchmark(BlobSize, BlobCount, Order));
    TestData data(BlobSize, BlobCount, Order);
    bool success = data.RunTests();
    ASSERT_TRUE(EndBlobfsBenchmark()); // clean up
    ASSERT_TRUE(success);
    END_TEST;
}

// Sets start_time to current time reported by rtc
// Returns 0 on success, -1 otherwise
int GetStartTime() {
    int rtc_fd = open("/dev/sys/acpi/rtc/rtc", O_RDONLY);
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
bool GenerateBlob(fbl::unique_ptr<BlobInfo>* out, size_t blob_size) {
    // Generate a Blob of random data
    fbl::AllocChecker ac;
    fbl::unique_ptr<BlobInfo> info(new (&ac) BlobInfo);
    EXPECT_EQ(ac.check(), true);
    info->data.reset(new (&ac) char[blob_size]);
    EXPECT_EQ(ac.check(), true);
    unsigned int seed = static_cast<unsigned int>(zx_ticks_get());
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
              ZX_OK, "Couldn't create Merkle Tree");
    strcpy(info->path, kMountPath);
    size_t prefix_len = strlen(info->path);
    info->path[prefix_len++] = '/';
    digest.ToString(info->path + prefix_len, sizeof(info->path) - prefix_len);

    // Sanity-check the merkle tree
    ASSERT_EQ(MerkleTree::Verify(&info->data[0], info->size_data, &info->merkle[0],
                                 info->size_merkle, 0, info->size_data, digest),
              ZX_OK, "Failed to validate Merkle Tree");

    *out = fbl::move(info);
    return true;
}

// Helper for streaming operations (such as read, write) which may need to be
// repeated multiple times.
template <typename T, typename U>
inline int StreamAll(T func, int fd, U* buf, size_t max) {
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

} // namespace

TestData::TestData(size_t blob_size, size_t blob_count, TraversalOrder order_)
    : blob_size_(blob_size), blob_count_(blob_count), order_(order_) {
    indices_ = new size_t[blob_count_];

    paths_ = new char*[blob_count_];
    for (size_t i = 0; i < blob_count; i++) {
        paths_[i] = new char[PATH_MAX];
    }

    samples_ = new zx_time_t*[kNameCount];
    for (int i = 0; i < static_cast<int>(TestName::kCount); i++) {
        samples_[i] = new zx_time_t[GetMaxCount()];
    }

    GenerateOrder();
}

TestData::~TestData() {
    for (int i = 0; i < kNameCount; i++) {
        delete[] samples_[i];
    }

    for (size_t i = 0; i < blob_count_; i++) {
        delete[] paths_[i];
    }

    delete[] indices_;
    delete[] samples_;
    delete[] paths_;
}

bool TestData::RunTests() {
    ASSERT_TRUE(CreateBlobs());
    ASSERT_TRUE(ReadBlobs());
    ASSERT_TRUE(UnlinkBlobs());
    ASSERT_TRUE(Sync());
    return true;
}

void TestData::GenerateOrder() {
    size_t max = blob_count_ - 1;

    if (order_ == TraversalOrder::kRandom) {
        memset(indices_, 0, sizeof(size_t) * blob_count_);
        srand(static_cast<unsigned>(zx_ticks_get()));
    }

    while (true) {
        switch (order_) {
        case TraversalOrder::kLast:
        case TraversalOrder::kReverse: {
            indices_[max] = blob_count_ - max - 1;
            break;
        }
        case TraversalOrder::kRandom: {
            if (max == 0) {
                break;
            }

            size_t index = rand() % max;
            size_t selected = indices_[index]; // random number we selected
            size_t swap = indices_[max];       // start randomizing at end of array

            if (selected == 0 && index != 0) {
                selected = index; // set value if it has not already been set
            }

            if (swap == 0) {
                swap = max; // set value if it has not already been set
            }

            indices_[index] = swap;
            indices_[max] = selected;
            break;
        }
        default: {
            indices_[max] = max;
            break;
        }
        }

        if (max == 0) {
            break;
        }
        max--;
    }
}

size_t TestData::GetMaxCount() {
    if (order_ == TraversalOrder::kFirst || order_ == TraversalOrder::kLast) {
        return kEndCount;
    }

    return blob_count_;
}

void TestData::GetNameStr(TestName name, char* name_str) {
    switch (name) {
    case TestName::kCreate:
        strcpy(name_str, "create");
        break;
    case TestName::kTruncate:
        strcpy(name_str, "truncate");
        break;
    case TestName::kWrite:
        strcpy(name_str, "write");
        break;
    case TestName::kOpen:
        strcpy(name_str, "open");
        break;
    case TestName::kRead:
        strcpy(name_str, "read");
        break;
    case TestName::kClose:
        strcpy(name_str, "close");
        break;
    case TestName::kUnlink:
        strcpy(name_str, "unlink");
        break;
    default:
        strcpy(name_str, "unknown");
        break;
    }
}

void TestData::GetOrderStr(char* order_str) {
    switch (order_) {
    case TraversalOrder::kReverse:
        strcpy(order_str, "reverse");
        break;
    case TraversalOrder::kRandom:
        strcpy(order_str, "random");
        break;
    case TraversalOrder::kFirst:
        strcpy(order_str, "first");
        break;
    case TraversalOrder::kLast:
        strcpy(order_str, "last");
        break;
    default:
        strcpy(order_str, "default");
        break;
    }
}

void TestData::PrintOrder() {
    for (size_t i = 0; i < blob_count_; i++) {
        printf("Index %lu: %lu\n", i, indices_[i]);
    }
}

inline void TestData::SampleEnd(zx_time_t start, TestName name, size_t index) {
    zx_time_t now = zx_ticks_get();
    samples_[static_cast<int>(name)][index] = now - start;
}

bool TestData::ReportTest(TestName name) {
    zx_time_t ticks_per_msec = zx_ticks_per_second() / 1000;

    double min = DBL_MAX;
    double max = 0;
    double avg = 0;
    double stddev = 0;
    zx_time_t total = 0;

    size_t sample_count = GetMaxCount();
    double samples_ms[sample_count];
    zx_time_t* test_samples = samples_[static_cast<int>(name)];

    for (size_t i = 0; i < sample_count; i++) {
        samples_ms[i] = static_cast<double>(test_samples[i]) /
                        static_cast<double>(ticks_per_msec);
        avg += samples_ms[i];
        total += test_samples[i];

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

    char test_name[kTestNameMaxLength];
    char test_order_[kTestOrderMaxLength];
    GetNameStr(name, test_name);
    GetOrderStr(test_order_);
    printf(
        "\nBenchmark %*s: [%10lu] msec,"
        " average: [%8.2f] msec, min: [%8.2f] msec,"
        " max: [%8.2f] msec - %lu outliers (above [%8.2f] msec)",
        static_cast<int>(kTestNameMaxLength), test_name, total, avg, min, max,
        outlier_count, outlier);

    FILE* results = fopen(kOutputPath, "a");

    ASSERT_NONNULL(results, "Failed to open results file");

    fprintf(results, "%lu,%lu,%s,%s,%s,%f,%f,%f,%f,%f,%lu\n", blob_size_,
            blob_count_, start_time, test_name, test_order_, avg, min, max,
            stddev, outlier, outlier_count);
    fclose(results);

    test_name[0] = '\0';
    return true;
}

bool TestData::CreateBlobs() {
    size_t sample_index = 0;

    for (size_t i = 0; i < blob_count_; i++) {
        bool record =
            (order_ != TraversalOrder::kFirst && order_ != TraversalOrder::kLast);
        record |= (order_ == TraversalOrder::kFirst && i < kEndCount);
        record |= (order_ == TraversalOrder::kLast &&
                   i >= static_cast<int>(TraversalOrder::kLast) - kEndCount);

        fbl::unique_ptr<BlobInfo> info;
        ASSERT_TRUE(GenerateBlob(&info, blob_size_));
        strcpy(paths_[i], info->path);

        // create
        zx_time_t start = zx_ticks_get();
        int fd = open(info->path, O_CREAT | O_RDWR);
        if (record) {
            SampleEnd(start, TestName::kCreate, sample_index);
        }

        ASSERT_GT(fd, 0, "Failed to create blob");

        // truncate
        start = zx_ticks_get();
        ASSERT_EQ(ftruncate(fd, blob_size_), 0, "Failed to truncate blob");
        if (record) {
            SampleEnd(start, TestName::kTruncate, sample_index);
        }

        // write
        start = zx_ticks_get();
        ASSERT_EQ(StreamAll(write, fd, info->data.get(), blob_size_), 0,
                  "Failed to write Data");
        if (record) {
            SampleEnd(start, TestName::kWrite, sample_index);
        }

        ASSERT_EQ(close(fd), 0, "Failed to close blob");

        if (record) {
            sample_index++;
        }
    }

    ASSERT_TRUE(ReportTest(TestName::kCreate));
    ASSERT_TRUE(ReportTest(TestName::kTruncate));
    ASSERT_TRUE(ReportTest(TestName::kWrite));

    return true;
}

bool TestData::ReadBlobs() {
    for (size_t i = 0; i < GetMaxCount(); i++) {
        size_t index = indices_[i];
        const char* path = paths_[index];

        // open
        zx_time_t start = zx_ticks_get();
        int fd = open(path, O_RDONLY);
        SampleEnd(start, TestName::kOpen, i);
        ASSERT_GT(fd, 0, "Failed to open blob");

        fbl::AllocChecker ac;
        fbl::unique_ptr<char[]> buf(new (&ac) char[blob_size_]);
        EXPECT_EQ(ac.check(), true);
        ASSERT_EQ(lseek(fd, 0, SEEK_SET), 0);

        // read
        start = zx_ticks_get();
        bool success = StreamAll(read, fd, &buf[0], blob_size_);
        SampleEnd(start, TestName::kRead, i);

        // close
        start = zx_ticks_get();
        ASSERT_EQ(close(fd), 0, "Failed to close blob");
        SampleEnd(start, TestName::kClose, i);

        ASSERT_EQ(success, 0, "Failed to read data");
    }

    ASSERT_TRUE(ReportTest(TestName::kOpen));
    ASSERT_TRUE(ReportTest(TestName::kRead));
    ASSERT_TRUE(ReportTest(TestName::kClose));
    return true;
}

bool TestData::UnlinkBlobs() {
    for (size_t i = 0; i < GetMaxCount(); i++) {
        size_t index = indices_[i];
        const char* path = paths_[index];

        // unlink
        zx_time_t start = zx_ticks_get();
        ASSERT_EQ(unlink(path), 0, "Failed to unlink");
        SampleEnd(start, TestName::kUnlink, i);
    }

    ASSERT_TRUE(ReportTest(TestName::kUnlink));
    return true;
}

bool TestData::Sync() {
    int fd = open(kMountPath, O_DIRECTORY | O_RDONLY);
    ASSERT_GE(fd, 0);
    ASSERT_EQ(syncfs(fd), 0);
    ASSERT_EQ(close(fd), 0);
    return true;
}

namespace {

BEGIN_TEST_CASE(blobfs_benchmarks)

RUN_FOR_ALL_ORDER(RunBasicBlobBenchmark, 128, 500);
RUN_FOR_ALL_ORDER(RunBasicBlobBenchmark, 128, 1000);
RUN_FOR_ALL_ORDER(RunBasicBlobBenchmark, 128, 10000);

RUN_FOR_ALL_ORDER(RunBasicBlobBenchmark, 512, 500);
RUN_FOR_ALL_ORDER(RunBasicBlobBenchmark, 512, 1000);
RUN_FOR_ALL_ORDER(RunBasicBlobBenchmark, 512, 10000);

RUN_FOR_ALL_ORDER(RunBasicBlobBenchmark, kKb, 500);
RUN_FOR_ALL_ORDER(RunBasicBlobBenchmark, kKb, 1000);
RUN_FOR_ALL_ORDER(RunBasicBlobBenchmark, kKb, 10000);

RUN_FOR_ALL_ORDER(RunBasicBlobBenchmark, 128 * kKb, 500);
RUN_FOR_ALL_ORDER(RunBasicBlobBenchmark, 128 * kKb, 1000);
RUN_FOR_ALL_ORDER(RunBasicBlobBenchmark, 128 * kKb, 10000);

RUN_FOR_ALL_ORDER(RunBasicBlobBenchmark, 512 * kKb, 500);
RUN_FOR_ALL_ORDER(RunBasicBlobBenchmark, 512 * kKb, 1000);
RUN_FOR_ALL_ORDER(RunBasicBlobBenchmark, 512 * kKb, 10000);

RUN_FOR_ALL_ORDER(RunBasicBlobBenchmark, kMb, 500);
RUN_FOR_ALL_ORDER(RunBasicBlobBenchmark, kMb, 1000);

END_TEST_CASE(blobfs_benchmarks)

} // namespace

int main(int argc, char** argv) {
    if (GetStartTime() != 0) {
        printf("Unable to get start time for test\n");
    }

    return unittest_run_all_tests(argc, argv) ? 0 : -1;
}
