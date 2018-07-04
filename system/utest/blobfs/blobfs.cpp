// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <assert.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <threads.h>
#include <utime.h>

#include <blobfs/format.h>
#include <blobfs/lz4.h>
#include <digest/digest.h>
#include <digest/merkle-tree.h>
#include <fbl/alloc_checker.h>
#include <fbl/atomic.h>
#include <fbl/auto_call.h>
#include <fbl/auto_lock.h>
#include <fbl/intrusive_double_list.h>
#include <fbl/unique_fd.h>
#include <fbl/unique_ptr.h>
#include <fs-management/fvm.h>
#include <fs-management/mount.h>
#include <fs-management/ramdisk.h>
#include <fvm/fvm.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/fdio/io.h>
#include <lib/memfs/memfs.h>
#include <unittest/unittest.h>
#include <zircon/device/device.h>
#include <zircon/device/ramdisk.h>
#include <zircon/device/vfs.h>
#include <zircon/syscalls.h>

#include "blobfs-test.h"

#define TMPFS_PATH "/blobfs-tmp"
#define MOUNT_PATH "/blobfs-tmp/zircon-blobfs-test"

namespace {
using digest::Digest;
using digest::MerkleTree;

#define RUN_TEST_FOR_ALL_TYPES(test_size, test_name) \
    RUN_TEST_##test_size(test_name<FsTestType::kNormal>)  \
    RUN_TEST_##test_size(test_name<FsTestType::kFvm>)

#define FVM_DRIVER_LIB "/boot/driver/fvm.so"
#define STRLEN(s) sizeof(s) / sizeof((s)[0])

// FVM slice size used for tests
constexpr size_t kTestFvmSliceSize = 8 * (1 << 10); // 8kb
// Minimum blobfs size required by CreateUmountRemountLargeMultithreaded test
constexpr size_t kBytesNormalMinimum = 5 * (1 << 20); // 5mb
// Minimum blobfs size required by ResizePartition test
constexpr size_t kSliceBytesFvmMinimum = 507 * kTestFvmSliceSize;
constexpr size_t kTotalBytesFvmMinimum = fvm::MetadataSize(kSliceBytesFvmMinimum,
                                         kTestFvmSliceSize) * 2 + kSliceBytesFvmMinimum; // ~8.5mb

constexpr uint8_t kTestUniqueGUID[] = {
    0xFF, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
    0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f
};
constexpr uint8_t kTestPartGUID[] = {
    0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f,
    0xFF, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07
};

constexpr size_t kBlocksPerSlice = kTestFvmSliceSize / blobfs::kBlobfsBlockSize;

const fsck_options_t test_fsck_options = {
    .verbose = false,
    .never_modify = true,
    .always_modify = false,
    .force = true,
};

// Information about the real disk which must be constructed at runtime, but which persists
// between tests.
static bool gUseRealDisk = false;
struct real_disk_info {
    uint64_t blk_size;
    uint64_t blk_count;
    char disk_path[PATH_MAX];
} gRealDiskInfo;

static_assert(fbl::is_pod<real_disk_info>::value, "Global variables should contain exclusively POD"
                                                  "data");

BlobfsTest::~BlobfsTest() {
    switch (state_) {
    case FsTestState::kMinimal:
    case FsTestState::kRunning:
    case FsTestState::kError:
        EXPECT_EQ(Teardown(FsTestState::kMinimal), 0);
        break;
    default:
        break;
    }
}

bool BlobfsTest::Init(FsTestState state) {
    BEGIN_HELPER;
    ASSERT_EQ(state_, FsTestState::kInit);
    auto error = fbl::MakeAutoCall([this](){ state_ = FsTestState::kError; });

    ASSERT_TRUE(mkdir(MOUNT_PATH, 0755) == 0 || errno == EEXIST,
                "Could not create mount point for test filesystems");

    if (gUseRealDisk) {
        strncpy(ramdisk_path_, gRealDiskInfo.disk_path, PATH_MAX);
        blk_size_ = gRealDiskInfo.blk_size;
        blk_count_ = gRealDiskInfo.blk_count;
    } else {
        ASSERT_EQ(create_ramdisk(blk_size_, blk_count_, ramdisk_path_), 0,
                  "Blobfs: Could not create ramdisk");
    }

    if (type_ == FsTestType::kFvm) {
        ASSERT_EQ(kTestFvmSliceSize % blobfs::kBlobfsBlockSize, 0);

        fbl::unique_fd fd(open(ramdisk_path_, O_RDWR));
        ASSERT_TRUE(fd, "[FAILED]: Could not open test disk");
        ASSERT_EQ(fvm_init(fd.get(), kTestFvmSliceSize), ZX_OK,
                  "[FAILED]: Could not format disk with FVM");
        ASSERT_GE(ioctl_device_bind(fd.get(), FVM_DRIVER_LIB, STRLEN(FVM_DRIVER_LIB)), 0,
                  "[FAILED]: Could not bind disk to FVM driver");

        snprintf(fvm_path_, sizeof(fvm_path_), "%s/fvm", ramdisk_path_);
        ASSERT_EQ(wait_for_device(fvm_path_, ZX_SEC(3)), ZX_OK,
                  "[FAILED]: FVM driver never appeared");
        fd.reset();

        // Open "fvm" driver.
        fbl::unique_fd fvm_fd(open(fvm_path_, O_RDWR));
        ASSERT_GE(fvm_fd.get(), 0, "[FAILED]: Could not open FVM driver");

        // Restore the "fvm_disk_path" to the ramdisk, so it can
        // be destroyed when the test completes.
        fvm_path_[strlen(fvm_path_) - strlen("/fvm")] = 0;

        alloc_req_t request;
        memset(&request, 0, sizeof(request));
        request.slice_count = 1;
        strcpy(request.name, "fs-test-partition");
        memcpy(request.type, kTestPartGUID, sizeof(request.type));
        memcpy(request.guid, kTestUniqueGUID, sizeof(request.guid));

        fd.reset(fvm_allocate_partition(fvm_fd.get(), &request));
        ASSERT_TRUE(fd, "[FAILED]: Could not allocate FVM partition");
        fvm_fd.reset();
        fd.reset(open_partition(kTestUniqueGUID, kTestPartGUID, 0, ramdisk_path_));
        ASSERT_TRUE(fd, "[FAILED]: Could not locate FVM partition");
        fd.reset();
    }

    if (state != FsTestState::kMinimal) {
        ASSERT_EQ(state, FsTestState::kRunning);
        ASSERT_EQ(mkfs(ramdisk_path_, DISK_FORMAT_BLOBFS, launch_stdio_sync, &default_mkfs_options),
                  ZX_OK);
        ASSERT_TRUE(Mount());
    }

    error.cancel();
    state_ = state;
    END_HELPER;
}

bool BlobfsTest::Remount() {
    BEGIN_HELPER;
    ASSERT_EQ(state_, FsTestState::kRunning);
    auto error = fbl::MakeAutoCall([this](){ state_ = FsTestState::kError; });
    ASSERT_EQ(umount(MOUNT_PATH), ZX_OK, "Failed to unmount blobfs");
    ASSERT_EQ(fsck(ramdisk_path_, DISK_FORMAT_BLOBFS, &test_fsck_options,
                   launch_stdio_sync), ZX_OK, "Filesystem fsck failed");
    ASSERT_TRUE(Mount(), "Failed to mount blobfs");
    error.cancel();
    END_HELPER;
}

bool BlobfsTest::Teardown(FsTestState state) {
    BEGIN_HELPER;
    ASSERT_NE(state_, FsTestState::kComplete);
    auto error = fbl::MakeAutoCall([this](){ state_ = FsTestState::kError; });

    if (state != FsTestState::kMinimal) {
        ASSERT_EQ(state, FsTestState::kRunning);
        ASSERT_TRUE(CheckInfo(MOUNT_PATH));
        ASSERT_EQ(umount(MOUNT_PATH), ZX_OK, "Failed to unmount filesystem");
        ASSERT_EQ(fsck(ramdisk_path_, DISK_FORMAT_BLOBFS, &test_fsck_options, launch_stdio_sync),
                  ZX_OK, "Filesystem fsck failed");
    }

    if (gUseRealDisk) {
        if (type_ == FsTestType::kFvm) {
            ASSERT_EQ(fvm_destroy(fvm_path_), ZX_OK);
        }
    } else {
        if (type_ == FsTestType::kFvm) {
            ASSERT_EQ(destroy_ramdisk(fvm_path_), 0);
        } else {
            ASSERT_EQ(destroy_ramdisk(ramdisk_path_), 0);
        }
    }

    error.cancel();
    state_ = FsTestState::kComplete;
    END_HELPER;
}

bool BlobfsTest::GetDevicePath(char* path, size_t len) const {
    BEGIN_HELPER;
    if (type_ == FsTestType::kFvm) {
        strlcpy(path, fvm_path_, len);
        strlcat(path, "/fvm", len);
        while (true) {
            DIR* dir = opendir(path);
            ASSERT_NONNULL(dir, "Unable to open FVM dir");

            struct dirent* dir_entry;
            bool updated = false;
            while ((dir_entry = readdir(dir)) != NULL) {
                if (strcmp(dir_entry->d_name, ".") == 0) {
                    continue;
                }

                updated = true;
                strlcat(path, "/", len);
                strlcat(path, dir_entry->d_name, len);
            }

            closedir(dir);

            if (!updated) {
                break;
            }
        }
    } else {
        strlcpy(path, ramdisk_path_, len);
    }
    END_HELPER;
}

bool BlobfsTest::ToggleSleep() {
    BEGIN_HELPER;

    if (asleep_) {
        // If the ramdisk is asleep, wake it up.
        if (type_ == FsTestType::kNormal) {
            ASSERT_EQ(wake_ramdisk(ramdisk_path_), 0);
        } else {
            ASSERT_EQ(wake_ramdisk(fvm_path_), 0);
        }
    } else {
        // If the ramdisk is active, put it to sleep.
        if (type_ == FsTestType::kNormal) {
            ASSERT_EQ(sleep_ramdisk(ramdisk_path_, 0), 0);
        } else {
            ASSERT_EQ(sleep_ramdisk(fvm_path_, 0), 0);
        }
    }

    asleep_ = !asleep_;
    END_HELPER;
}

bool BlobfsTest::CheckInfo(const char* mount_path) {
    fbl::unique_fd fd(open(mount_path, O_RDONLY | O_DIRECTORY));
    ASSERT_TRUE(fd);
    char buf[sizeof(vfs_query_info_t) + MAX_FS_NAME_LEN + 1];
    vfs_query_info_t* info = reinterpret_cast<vfs_query_info_t*>(buf);
    ssize_t r = ioctl_vfs_query_fs(fd.get(), info, sizeof(buf) - 1);
    ASSERT_EQ(r, (ssize_t)(sizeof(vfs_query_info_t) + strlen("blobfs")), "Failed to query filesystem");
    buf[r] = '\0';
    const char* name = reinterpret_cast<const char*>(buf + sizeof(vfs_query_info_t));
    ASSERT_EQ(strncmp("blobfs", name, strlen("blobfs")), 0, "Unexpected filesystem mounted");
    ASSERT_LE(info->used_nodes, info->total_nodes, "Used nodes greater than free nodes");
    ASSERT_LE(info->used_bytes, info->total_bytes, "Used bytes greater than free bytes");
    ASSERT_EQ(close(fd.release()), 0);
    return true;
}

bool BlobfsTest::Mount() {
    BEGIN_HELPER;
    int flags = read_only_ ? O_RDONLY : O_RDWR;

    fbl::unique_fd fd(open(ramdisk_path_, flags));
    ASSERT_TRUE(fd.get(), "Could not open ramdisk");

    mount_options_t options;
    memcpy(&options, &default_mount_options, sizeof(options));

    if (read_only_) {
        options.readonly = true;
    }

    auto launch = stdio_ ? launch_stdio_async : launch_silent_async;

    // fd consumed by mount. By default, mount waits until the filesystem is
    // ready to accept commands.
    ASSERT_EQ(mount(fd.get(), MOUNT_PATH, DISK_FORMAT_BLOBFS, &options, launch), ZX_OK,
              "Could not mount blobfs");

    END_HELPER;
}

// Helper functions for testing:

// Helper for streaming operations (such as read, write) which may need to be
// repeated multiple times.
template <typename T, typename U>
static int StreamAll(T func, int fd, U* buf, size_t max) {
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

static bool VerifyContents(int fd, const char* data, size_t size_data) {
    // Verify the contents of the Blob
    fbl::AllocChecker ac;
    fbl::unique_ptr<char[]> buf(new (&ac) char[size_data]);
    EXPECT_EQ(ac.check(), true);
    ASSERT_EQ(lseek(fd, 0, SEEK_SET), 0);
    ASSERT_EQ(StreamAll(read, fd, &buf[0], size_data), 0, "Failed to read data");
    ASSERT_EQ(memcmp(buf.get(), data, size_data), 0, "Read data, but it was bad");
    return true;
}

// An in-memory representation of a blob.
typedef struct blob_info {
    char path[PATH_MAX];
    fbl::unique_ptr<char[]> merkle;
    size_t size_merkle;
    fbl::unique_ptr<char[]> data;
    size_t size_data;
} blob_info_t;

// Creates an open blob with the provided Merkle tree + Data, and
// reads to verify the data.
static bool MakeBlob(blob_info_t* info, fbl::unique_fd* out_fd) {
    fbl::unique_fd fd(open(info->path, O_CREAT | O_RDWR));
    ASSERT_TRUE(fd, "Failed to create blob");
    ASSERT_EQ(ftruncate(fd.get(), info->size_data), 0);
    ASSERT_EQ(StreamAll(write, fd.get(), info->data.get(), info->size_data), 0,
              "Failed to write Data");
    ASSERT_TRUE(VerifyContents(fd.get(), info->data.get(), info->size_data));
    out_fd->reset(fd.release());
    return true;
}

static bool MakeBlobUnverified(blob_info_t* info, fbl::unique_fd* out_fd) {
    fbl::unique_fd fd(open(info->path, O_CREAT | O_RDWR));
    ASSERT_TRUE(fd, "Failed to create blob");
    ASSERT_EQ(ftruncate(fd.get(), info->size_data), 0);
    ASSERT_EQ(StreamAll(write, fd.get(), info->data.get(), info->size_data), 0,
              "Failed to write Data");
    out_fd->reset(fd.release());
    return true;
}

static bool VerifyCompromised(int fd, const char* data, size_t size_data) {
    // Verify the contents of the Blob
    fbl::AllocChecker ac;
    fbl::unique_ptr<char[]> buf(new (&ac) char[size_data]);
    EXPECT_EQ(ac.check(), true);

    ASSERT_EQ(lseek(fd, 0, SEEK_SET), 0);
    ASSERT_EQ(StreamAll(read, fd, &buf[0], size_data), -1, "Expected reading to fail");
    return true;
}

// Creates a blob with the provided Merkle tree + Data, and
// reads to verify the data.
static bool MakeBlobCompromised(blob_info_t* info) {
    fbl::unique_fd fd(open(info->path, O_CREAT | O_RDWR));
    ASSERT_TRUE(fd, "Failed to create blob");
    ASSERT_EQ(ftruncate(fd.get(), info->size_data), 0);

    // If we're writing a blob with invalid sizes, it's possible that writing will fail.
    StreamAll(write, fd.get(), info->data.get(), info->size_data);

    ASSERT_TRUE(VerifyCompromised(fd.get(), info->data.get(), info->size_data));
    ASSERT_EQ(close(fd.release()), 0);
    return true;
}

static bool uint8_to_hex_str(const uint8_t* data, char* hex_str) {
    for (size_t i = 0; i < 32; i++) {
        ASSERT_EQ(sprintf(hex_str + (i * 2), "%02x", data[i]), 2,
                  "Error converting name to string");
    }
    hex_str[64] = 0;
    return true;
}

static void RandomFill(char* data, size_t length) {
    static unsigned int seed = static_cast<unsigned int>(zx_ticks_get());
    // TODO(US-286): Make this easier to reproduce with reliably generated prng.
    unittest_printf("RandomFill of %zu bytes with seed: %u\n", length, seed);
    for (size_t i = 0; i < length; i++) {
        data[i] = (char)rand_r(&seed);
    }
}

using BlobSrcFunction = void (*)(char* data, size_t length);

// Creates, writes, reads (to verify) and operates on a blob.
// Returns the result of the post-processing 'func' (true == success).
static bool GenerateBlob(BlobSrcFunction sourceCb, size_t size_data,
                         fbl::unique_ptr<blob_info_t>* out) {
    BEGIN_HELPER;
    fbl::AllocChecker ac;
    fbl::unique_ptr<blob_info_t> info(new (&ac) blob_info_t);
    EXPECT_EQ(ac.check(), true);
    info->data.reset(new (&ac) char[size_data]);
    EXPECT_EQ(ac.check(), true);
    sourceCb(info->data.get(), size_data);
    info->size_data = size_data;

    // Generate the Merkle Tree
    info->size_merkle = MerkleTree::GetTreeLength(size_data);
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
    strcpy(info->path, MOUNT_PATH "/");
    size_t prefix_len = strlen(info->path);
    digest.ToString(info->path + prefix_len, sizeof(info->path) - prefix_len);

    // Sanity-check the merkle tree
    ASSERT_EQ(MerkleTree::Verify(&info->data[0], info->size_data, &info->merkle[0],
                                 info->size_merkle, 0, info->size_data, digest),
              ZX_OK, "Failed to validate Merkle Tree");

    *out = fbl::move(info);
    END_HELPER;
}

static bool GenerateRandomBlob(size_t size_data, fbl::unique_ptr<blob_info_t>* out) {
    BEGIN_HELPER;
    ASSERT_TRUE(GenerateBlob(RandomFill, size_data, out));
    END_HELPER;
}

bool QueryInfo(size_t expected_nodes, size_t expected_bytes) {
    fbl::unique_fd fd(open(MOUNT_PATH, O_RDONLY | O_DIRECTORY));
    ASSERT_TRUE(fd);

    char buf[sizeof(vfs_query_info_t) + MAX_FS_NAME_LEN + 1];
    vfs_query_info_t* info = reinterpret_cast<vfs_query_info_t*>(buf);
    ssize_t rv = ioctl_vfs_query_fs(fd.get(), info, sizeof(buf) - 1);
    ASSERT_EQ(close(fd.release()), 0);

    ASSERT_EQ(rv, sizeof(vfs_query_info_t) + strlen("blobfs"), "Failed to query filesystem");

    buf[rv] = '\0';  // NULL terminate the name.
    ASSERT_EQ(strncmp("blobfs", info->name, strlen("blobfs")), 0);
    ASSERT_EQ(info->block_size, blobfs::kBlobfsBlockSize);
    ASSERT_EQ(info->max_filename_size, Digest::kLength * 2);
    ASSERT_EQ(info->fs_type, VFS_TYPE_BLOBFS);
    ASSERT_NE(info->fs_id, 0);

    // Check that used_bytes are within a reasonable range
    ASSERT_GE(info->used_bytes, expected_bytes);
    ASSERT_LE(info->used_bytes, info->total_bytes);

    // Check that total_bytes are a multiple of slice_size
    ASSERT_GE(info->total_bytes, kTestFvmSliceSize);
    ASSERT_EQ(info->total_bytes % kTestFvmSliceSize, 0);
    ASSERT_EQ(info->total_nodes, kTestFvmSliceSize / blobfs::kBlobfsInodeSize);
    ASSERT_EQ(info->used_nodes, expected_nodes);
    return true;
}

}  // namespace

// Actual tests:
template <FsTestType TestType>
static bool TestBasic(void) {
    BEGIN_TEST;
    BlobfsTest blobfsTest(TestType);
    ASSERT_TRUE(blobfsTest.Init(), "Mounting Blobfs");
    for (size_t i = 10; i < 16; i++) {
        fbl::unique_ptr<blob_info_t> info;
        ASSERT_TRUE(GenerateRandomBlob(1 << i, &info));

        fbl::unique_fd fd;
        ASSERT_TRUE(MakeBlob(info.get(), &fd));
        ASSERT_EQ(close(fd.release()), 0);

        // We can re-open and verify the Blob as read-only
        fd.reset(open(info->path, O_RDONLY));
        ASSERT_TRUE(fd, "Failed to-reopen blob");
        ASSERT_TRUE(VerifyContents(fd.get(), info->data.get(), info->size_data));
        ASSERT_EQ(close(fd.release()), 0);

        // We cannot re-open the blob as writable
        fd.reset(open(info->path, O_RDWR | O_CREAT));
        ASSERT_FALSE(fd, "Shouldn't be able to re-create blob that exists");
        fd.reset(open(info->path, O_RDWR));
        ASSERT_FALSE(fd, "Shouldn't be able to re-open blob as writable");
        fd.reset(open(info->path, O_WRONLY));
        ASSERT_FALSE(fd, "Shouldn't be able to re-open blob as writable");

        ASSERT_EQ(unlink(info->path), 0);
    }

    ASSERT_TRUE(blobfsTest.Teardown(), "Mounting Blobfs");
    END_TEST;
}

template <FsTestType TestType>
static bool TestNullBlob(void) {
    BEGIN_TEST;
    BlobfsTest blobfsTest(TestType);
    ASSERT_TRUE(blobfsTest.Init(), "Mounting Blobfs");
    fbl::unique_ptr<blob_info_t> info;
    ASSERT_TRUE(GenerateRandomBlob(0, &info));

    fbl::unique_fd fd(open(info->path, O_CREAT | O_EXCL | O_RDWR));
    ASSERT_TRUE(fd);
    ASSERT_EQ(ftruncate(fd.get(), 0), 0);
    char buf[1];
    ASSERT_EQ(read(fd.get(), &buf[0], 1), 0, "Null Blob should reach EOF immediately");
    ASSERT_EQ(close(fd.release()), 0);

    fd.reset(open(info->path, O_CREAT | O_EXCL | O_RDWR));
    ASSERT_FALSE(fd, "Null Blob should already exist");
    fd.reset(open(info->path, O_CREAT | O_RDWR));
    ASSERT_FALSE(fd, "Null Blob should not be openable as writable");

    fd.reset(open(info->path, O_RDONLY));
    ASSERT_TRUE(fd);
    ASSERT_EQ(close(fd.release()), 0);
    ASSERT_EQ(unlink(info->path), 0, "Null Blob should be unlinkable");

    ASSERT_TRUE(blobfsTest.Teardown(), "unmounting blobfs");
    END_TEST;
}

template <FsTestType TestType>
static bool TestCompressibleBlob(void) {
    BEGIN_TEST;
    BlobfsTest blobfsTest(TestType);
    ASSERT_TRUE(blobfsTest.Init(), "Mounting Blobfs");
    for (size_t i = 10; i < 22; i++) {
        fbl::unique_ptr<blob_info_t> info;

        // Create blobs which are trivially compressible.
        ASSERT_TRUE(GenerateBlob([](char* data, size_t length) {
            size_t i = 0;
            while (i < length) {
                size_t j = (rand() % (length - i)) + 1;
                memset(data, (char) j, j);
                data += j;
                i += j;
            }
        }, 1 << i, &info));

        fbl::unique_fd fd;
        ASSERT_TRUE(MakeBlob(info.get(), &fd));
        ASSERT_EQ(close(fd.release()), 0);

        // We can re-open and verify the Blob as read-only
        fd.reset(open(info->path, O_RDONLY));
        ASSERT_TRUE(fd, "Failed to-reopen blob");
        ASSERT_TRUE(VerifyContents(fd.get(), info->data.get(), info->size_data));
        ASSERT_EQ(close(fd.release()), 0);

        // We cannot re-open the blob as writable
        fd.reset(open(info->path, O_RDWR | O_CREAT));
        ASSERT_FALSE(fd, "Shouldn't be able to re-create blob that exists");
        fd.reset(open(info->path, O_RDWR));
        ASSERT_FALSE(fd, "Shouldn't be able to re-open blob as writable");
        fd.reset(open(info->path, O_WRONLY));
        ASSERT_FALSE(fd, "Shouldn't be able to re-open blob as writable");

        // Force decompression by remounting, re-accessing blob.
        ASSERT_TRUE(blobfsTest.Remount());
        fd.reset(open(info->path, O_RDONLY));
        ASSERT_TRUE(fd, "Failed to-reopen blob");
        ASSERT_TRUE(VerifyContents(fd.get(), info->data.get(), info->size_data));
        ASSERT_EQ(close(fd.release()), 0);

        ASSERT_EQ(unlink(info->path), 0);
    }

    ASSERT_TRUE(blobfsTest.Teardown(), "Mounting Blobfs");
    END_TEST;
}

template <FsTestType TestType>
static bool TestMmap(void) {
    BEGIN_TEST;
    BlobfsTest blobfsTest(TestType);
    ASSERT_TRUE(blobfsTest.Init(), "Mounting Blobfs");

    for (size_t i = 10; i < 16; i++) {
        fbl::unique_ptr<blob_info_t> info;
        ASSERT_TRUE(GenerateRandomBlob(1 << i, &info));

        fbl::unique_fd fd;
        ASSERT_TRUE(MakeBlob(info.get(), &fd));
        ASSERT_EQ(close(fd.release()), 0);
        fd.reset(open(info->path, O_RDONLY));
        ASSERT_TRUE(fd, "Failed to-reopen blob");

        void* addr = mmap(NULL, info->size_data, PROT_READ, MAP_PRIVATE,
                          fd.get(), 0);
        ASSERT_NE(addr, MAP_FAILED, "Could not mmap blob");
        ASSERT_EQ(memcmp(addr, info->data.get(), info->size_data), 0, "Mmap data invalid");
        ASSERT_EQ(munmap(addr, info->size_data), 0, "Could not unmap blob");
        ASSERT_EQ(close(fd.release()), 0);
        ASSERT_EQ(unlink(info->path), 0);
    }
    ASSERT_TRUE(blobfsTest.Teardown(), "unmounting blobfs");
    END_TEST;
}

template <FsTestType TestType>
static bool TestMmapUseAfterClose(void) {
    BEGIN_TEST;
    BlobfsTest blobfsTest(TestType);
    ASSERT_TRUE(blobfsTest.Init(), "Mounting Blobfs");

    for (size_t i = 10; i < 16; i++) {
        fbl::unique_ptr<blob_info_t> info;
        ASSERT_TRUE(GenerateRandomBlob(1 << i, &info));

        fbl::unique_fd fd;
        ASSERT_TRUE(MakeBlob(info.get(), &fd));
        ASSERT_EQ(close(fd.release()), 0);
        fd.reset(open(info->path, O_RDONLY));
        ASSERT_TRUE(fd, "Failed to-reopen blob");

        void* addr = mmap(NULL, info->size_data, PROT_READ, MAP_PRIVATE, fd.get(), 0);
        ASSERT_NE(addr, MAP_FAILED, "Could not mmap blob");
        ASSERT_EQ(close(fd.release()), 0);

        // We should be able to access the mapped data while the file is closed.
        ASSERT_EQ(memcmp(addr, info->data.get(), info->size_data), 0, "Mmap data invalid");

        // We should be able to re-open and remap the file.
        //
        // Although this isn't being tested explicitly (we lack a mechanism to
        // check that the second mapping uses the same underlying pages as the
        // first) the memory usage should avoid duplication in the second
        // mapping.
        fd.reset(open(info->path, O_RDONLY));
        void* addr2 = mmap(NULL, info->size_data, PROT_READ, MAP_PRIVATE, fd.get(), 0);
        ASSERT_NE(addr2, MAP_FAILED, "Could not mmap blob");
        ASSERT_EQ(close(fd.release()), 0);
        ASSERT_EQ(memcmp(addr2, info->data.get(), info->size_data), 0, "Mmap data invalid");

        ASSERT_EQ(munmap(addr, info->size_data), 0, "Could not unmap blob");
        ASSERT_EQ(munmap(addr2, info->size_data), 0, "Could not unmap blob");

        ASSERT_EQ(unlink(info->path), 0);
    }
    ASSERT_TRUE(blobfsTest.Teardown(), "unmounting blobfs");
    END_TEST;
}

template <FsTestType TestType>
static bool TestReaddir(void) {
    BEGIN_TEST;
    BlobfsTest blobfsTest(TestType);
    ASSERT_TRUE(blobfsTest.Init(), "Mounting Blobfs");

    constexpr size_t kMaxEntries = 50;
    constexpr size_t kBlobSize = 1 << 10;

    fbl::AllocChecker ac;
    fbl::Array<fbl::unique_ptr<blob_info_t>>
        info(new (&ac) fbl::unique_ptr<blob_info_t>[kMaxEntries](), kMaxEntries);
    ASSERT_TRUE(ac.check());

    // Try to readdir on an empty directory
    DIR* dir = opendir(MOUNT_PATH);
    ASSERT_NONNULL(dir);
    ASSERT_NULL(readdir(dir), "Expected blobfs to start empty");

    // Fill a directory with entries
    for (size_t i = 0; i < kMaxEntries; i++) {
        ASSERT_TRUE(GenerateRandomBlob(kBlobSize, &info[i]));
        fbl::unique_fd fd;
        ASSERT_TRUE(MakeBlob(info[i].get(), &fd));
        ASSERT_EQ(close(fd.release()), 0);
        fd.reset(open(info[i]->path, O_RDONLY));
        ASSERT_TRUE(fd, "Failed to-reopen blob");
        ASSERT_TRUE(VerifyContents(fd.get(), info[i]->data.get(), info[i]->size_data));
        ASSERT_EQ(close(fd.release()), 0);
    }

    // Check that we see the expected number of entries
    size_t entries_seen = 0;
    struct dirent* de;
    while ((de = readdir(dir)) != nullptr) {
        entries_seen++;
    }
    ASSERT_EQ(entries_seen, kMaxEntries);
    entries_seen = 0;
    rewinddir(dir);

    // Readdir on a directory which contains entries, removing them as we go
    // along.
    while ((de = readdir(dir)) != nullptr) {
        for (size_t i = 0; i < kMaxEntries; i++) {
            if ((info[i]->size_data != 0) &&
                strcmp(strrchr(info[i]->path, '/') + 1, de->d_name) == 0) {
                ASSERT_EQ(unlink(info[i]->path), 0);
                // It's a bit hacky, but we set 'size_data' to zero
                // to identify the entry has been unlinked.
                info[i]->size_data = 0;
                goto found;
            }
        }
        ASSERT_TRUE(false, "Blobfs Readdir found an unexpected entry");
    found:
        entries_seen++;
    }
    ASSERT_EQ(entries_seen, kMaxEntries);

    ASSERT_NULL(readdir(dir), "Expected blobfs to end empty");

    ASSERT_EQ(closedir(dir), 0);
    ASSERT_TRUE(blobfsTest.Teardown(), "unmounting blobfs");
    END_TEST;
}


template <FsTestType TestType>
static bool TestDiskTooSmall(void) {
    BEGIN_TEST;

    if (gUseRealDisk) {
        fprintf(stderr, "Ramdisk required; skipping test\n");
        return true;
    }

    uint64_t minimum_size = 0;
    if (TestType == FsTestType::kFvm) {
        size_t blocks_per_slice = kTestFvmSliceSize / blobfs::kBlobfsBlockSize;

        // Calculate slices required for data blocks based on minimum requirement and slice size.
        uint64_t required_data_slices = fbl::round_up(blobfs::kMinimumDataBlocks, blocks_per_slice)
                                        / blocks_per_slice;
        // Require an additional 1 slice each for super, inode, and block bitmaps.
        uint64_t blobfs_size = (required_data_slices + 3) * kTestFvmSliceSize;
        minimum_size = blobfs_size;
        uint64_t metadata_size = fvm::MetadataSize(blobfs_size, kTestFvmSliceSize);

        // Re-calculate minimum size until the metadata size stops growing.
        while (minimum_size - blobfs_size != metadata_size * 2) {
            minimum_size = blobfs_size + metadata_size * 2;
            metadata_size = fvm::MetadataSize(minimum_size, kTestFvmSliceSize);
        }

        ASSERT_EQ(minimum_size - blobfs_size,
                  fvm::MetadataSize(minimum_size, kTestFvmSliceSize) * 2);
    } else {
        blobfs::blobfs_info_t info;
        info.inode_count = blobfs::kBlobfsDefaultInodeCount;
        info.block_count = blobfs::kMinimumDataBlocks;
        info.flags = 0;

        minimum_size = (blobfs::DataBlocks(info) + blobfs::DataStartBlock(info)) *
                       blobfs::kBlobfsBlockSize;
    }

    // Create disk with minimum possible size, make sure init passes.
    BlobfsTest blobfsTest(TestType);
    ASSERT_GE(minimum_size, blobfsTest.GetBlockSize());
    uint64_t disk_blocks = minimum_size / blobfsTest.GetBlockSize();
    ASSERT_TRUE(blobfsTest.SetBlockCount(disk_blocks));
    ASSERT_TRUE(blobfsTest.Init());
    ASSERT_TRUE(blobfsTest.Teardown());

    // Reset the disk size and test state.
    ASSERT_TRUE(blobfsTest.Reset());
    ASSERT_TRUE(blobfsTest.SetBlockCount(disk_blocks - 1));

    // Create disk with smaller than minimum size, make sure mkfs fails.
    ASSERT_TRUE(blobfsTest.Init(FsTestState::kMinimal));
    char device_path[PATH_MAX];
    ASSERT_TRUE(blobfsTest.GetDevicePath(device_path, PATH_MAX));
    ASSERT_NE(mkfs(device_path, DISK_FORMAT_BLOBFS, launch_stdio_sync, &default_mkfs_options),
              ZX_OK);
    ASSERT_TRUE(blobfsTest.Teardown(FsTestState::kMinimal));
    END_TEST;
}

template <FsTestType TestType>
static bool TestQueryInfo(void) {
    BEGIN_TEST;
    ASSERT_EQ(TestType, FsTestType::kFvm);

    BlobfsTest blobfsTest(TestType);
    ASSERT_TRUE(blobfsTest.Init(), "Mounting Blobfs");

    size_t total_bytes = 0;
    ASSERT_TRUE(QueryInfo(0, 0));
    for (size_t i = 10; i < 16; i++) {
        fbl::unique_ptr<blob_info_t> info;
        ASSERT_TRUE(GenerateRandomBlob(1 << i, &info));

        fbl::unique_fd fd;
        ASSERT_TRUE(MakeBlob(info.get(), &fd));
        ASSERT_EQ(close(fd.release()), 0);
        total_bytes += fbl::round_up(info->size_merkle + info->size_data,
                       blobfs::kBlobfsBlockSize);
    }

    ASSERT_TRUE(QueryInfo(6, total_bytes));
    ASSERT_TRUE(blobfsTest.Teardown(), "unmounting blobfs");
    END_TEST;
}

template <FsTestType TestType>
static bool UseAfterUnlink(void) {
    BEGIN_TEST;
    BlobfsTest blobfsTest(TestType);
    ASSERT_TRUE(blobfsTest.Init(), "Mounting Blobfs");

    for (size_t i = 0; i < 16; i++) {
        fbl::unique_ptr<blob_info_t> info;
        ASSERT_TRUE(GenerateRandomBlob(1 << i, &info));

        fbl::unique_fd fd;
        ASSERT_TRUE(MakeBlob(info.get(), &fd));

        // We should be able to unlink the blob
        ASSERT_EQ(unlink(info->path), 0, "Failed to unlink");

        // We should still be able to read the blob after unlinking
        ASSERT_TRUE(VerifyContents(fd.get(), info->data.get(), info->size_data));

        // After closing the fd, however, we should not be able to re-open the blob
        ASSERT_EQ(close(fd.release()), 0);
        ASSERT_LT(open(info->path, O_RDONLY), 0, "Expected blob to be deleted");
    }

    ASSERT_TRUE(blobfsTest.Teardown(), "unmounting blobfs");
    END_TEST;
}

template <FsTestType TestType>
static bool WriteAfterRead(void) {
    BEGIN_TEST;
    BlobfsTest blobfsTest(TestType);
    ASSERT_TRUE(blobfsTest.Init(), "Mounting Blobfs");;

    for (size_t i = 0; i < 16; i++) {
        fbl::unique_ptr<blob_info_t> info;
        ASSERT_TRUE(GenerateRandomBlob(1 << i, &info));

        fbl::unique_fd fd;
        ASSERT_TRUE(MakeBlob(info.get(), &fd));

        // After blob generation, writes should be rejected
        ASSERT_LT(write(fd.get(), info->data.get(), 1), 0,
                  "After being written, the blob should refuse writes");

        off_t seek_pos = (rand() % info->size_data);
        ASSERT_EQ(lseek(fd.get(), seek_pos, SEEK_SET), seek_pos);
        ASSERT_LT(write(fd.get(), info->data.get(), 1), 0,
                  "After being written, the blob should refuse writes");
        ASSERT_LT(ftruncate(fd.get(), rand() % info->size_data), 0,
                  "The blob should always refuse to be truncated");

        // We should be able to unlink the blob
        ASSERT_EQ(close(fd.release()), 0);
        ASSERT_EQ(unlink(info->path), 0, "Failed to unlink");
    }

    ASSERT_TRUE(blobfsTest.Teardown(), "unmounting blobfs");
    END_TEST;
}

template <FsTestType TestType>
bool WriteAfterUnlink(void) {
    BEGIN_TEST;
    BlobfsTest blobfsTest(TestType);
    ASSERT_TRUE(blobfsTest.Init(), "Mounting Blobfs");
    fbl::unique_ptr<blob_info_t> info;
    size_t size = 1 << 20;
    ASSERT_TRUE(GenerateRandomBlob(size, &info));

    // Partially write out first blob.
    fbl::unique_fd fd(open(info->path, O_CREAT | O_RDWR));
    ASSERT_TRUE(fd, "Failed to create blob");
    ASSERT_EQ(ftruncate(fd.get(), size), 0);
    ASSERT_EQ(StreamAll(write, fd.get(), info->data.get(), size / 2), 0, "Failed to write Data");

    ASSERT_EQ(unlink(info->path), 0);
    ASSERT_EQ(StreamAll(write, fd.get(), info->data.get() + size / 2, size - (size / 2)), 0, "Failed to write Data");
    ASSERT_EQ(close(fd.release()), 0);
    ASSERT_LT(open(info->path, O_RDONLY), 0);
    ASSERT_TRUE(blobfsTest.Teardown(), "unmounting Blobfs");
    END_TEST;
}

template <FsTestType TestType>
static bool ReadTooLarge(void) {
    BEGIN_TEST;
    BlobfsTest blobfsTest(TestType);
    ASSERT_TRUE(blobfsTest.Init(), "Mounting Blobfs");

    for (size_t i = 0; i < 16; i++) {
        fbl::unique_ptr<blob_info_t> info;
        ASSERT_TRUE(GenerateRandomBlob(1 << i, &info));

        fbl::unique_fd fd;
        ASSERT_TRUE(MakeBlob(info.get(), &fd));

        // Verify the contents of the Blob
        fbl::AllocChecker ac;
        fbl::unique_ptr<char[]> buf(new (&ac) char[info->size_data]);
        EXPECT_EQ(ac.check(), true);

        // Try read beyond end of blob
        off_t end_off = info->size_data;
        ASSERT_EQ(lseek(fd.get(), end_off, SEEK_SET), end_off);
        ASSERT_EQ(read(fd.get(), &buf[0], 1), 0, "Expected empty read beyond end of file");

        // Try some reads which straddle the end of the blob
        for (ssize_t j = 1; j < static_cast<ssize_t>(info->size_data); j *= 2) {
            end_off = info->size_data - j;
            ASSERT_EQ(lseek(fd.get(), end_off, SEEK_SET), end_off);
            ASSERT_EQ(read(fd.get(), &buf[0], j * 2), j, "Expected to only read one byte at end of file");
            ASSERT_EQ(memcmp(buf.get(), &info->data[info->size_data - j], j),
                      0, "Read data, but it was bad");
        }

        // We should be able to unlink the blob
        ASSERT_EQ(close(fd.release()), 0);
        ASSERT_EQ(unlink(info->path), 0, "Failed to unlink");
    }

    ASSERT_TRUE(blobfsTest.Teardown(), "unmounting blobfs");
    END_TEST;
}

template <FsTestType TestType>
static bool BadAllocation(void) {
    BEGIN_TEST;
    BlobfsTest blobfsTest(TestType);
    ASSERT_TRUE(blobfsTest.Init(), "Mounting Blobfs");

    ASSERT_LT(open(MOUNT_PATH "/00112233445566778899AABBCCDDEEFFGGHHIIJJKKLLMMNNOOPPQQRRSSTTUUVV",
                   O_CREAT | O_RDWR),
              0, "Only acceptable pathnames are hex");
    ASSERT_LT(open(MOUNT_PATH "/00112233445566778899AABBCCDDEEFF", O_CREAT | O_RDWR), 0,
              "Only acceptable pathnames are 32 hex-encoded bytes");

    fbl::unique_ptr<blob_info_t> info;
    ASSERT_TRUE(GenerateRandomBlob(1 << 15, &info));

    fbl::unique_fd fd(open(info->path, O_CREAT | O_RDWR));
    ASSERT_TRUE(fd, "Failed to create blob");
    ASSERT_EQ(ftruncate(fd.get(), 0), -1, "Blob without data doesn't match null blob");
    // This is the size of the entire disk; we won't have room.
    ASSERT_EQ(ftruncate(fd.get(), blobfsTest.GetDiskSize()), -1, "Huge blob");

    // Okay, finally, a valid blob!
    ASSERT_EQ(ftruncate(fd.get(), info->size_data), 0, "Failed to allocate blob");

    // Write nothing, but close the blob. Since the write was incomplete,
    // it will be inaccessible.
    ASSERT_EQ(close(fd.release()), 0);
    ASSERT_LT(open(info->path, O_RDWR), 0, "Cannot access partial blob");
    ASSERT_LT(open(info->path, O_RDONLY), 0, "Cannot access partial blob");

    // And once more -- let's write everything but the last byte of a blob's data.
    fd.reset(open(info->path, O_CREAT | O_RDWR));
    ASSERT_TRUE(fd, "Failed to create blob");
    ASSERT_EQ(ftruncate(fd.get(), info->size_data), 0, "Failed to allocate blob");
    ASSERT_EQ(StreamAll(write, fd.get(), info->data.get(), info->size_data - 1), 0,
              "Failed to write data");
    ASSERT_EQ(close(fd.release()), 0);
    ASSERT_LT(open(info->path, O_RDWR), 0, "Cannot access partial blob");

    ASSERT_TRUE(blobfsTest.Teardown(), "unmounting blobfs");
    END_TEST;
}

template <FsTestType TestType>
static bool CorruptedBlob(void) {
    BEGIN_TEST;
    BlobfsTest blobfsTest(TestType);
    // This test is noisy, since blob corruption is logged loudly.
    blobfsTest.SetStdio(false);
    ASSERT_TRUE(blobfsTest.Init(), "Mounting Blobfs");

    fbl::unique_ptr<blob_info_t> info;
    for (size_t i = 1; i < 18; i++) {
        ASSERT_TRUE(GenerateRandomBlob(1 << i, &info));
        info->size_data -= (rand() % info->size_data) + 1;
        if (info->size_data == 0) {
            info->size_data = 1;
        }
        ASSERT_TRUE(MakeBlobCompromised(info.get()));
    }

    for (size_t i = 0; i < 18; i++) {
        ASSERT_TRUE(GenerateRandomBlob(1 << i, &info));
        // Flip a random bit of the data
        size_t rand_index = rand() % info->size_data;
        char old_val = info->data.get()[rand_index];
        while ((info->data.get()[rand_index] = static_cast<char>(rand())) == old_val) {
        }
        ASSERT_TRUE(MakeBlobCompromised(info.get()));
    }

    ASSERT_TRUE(blobfsTest.Teardown(), "unmounting blobfs");
    END_TEST;
}

template <FsTestType TestType>
static bool CorruptedDigest(void) {
    BEGIN_TEST;
    BlobfsTest blobfsTest(TestType);
    // This test is noisy, since blob corruption is logged loudly.
    blobfsTest.SetStdio(false);
    ASSERT_TRUE(blobfsTest.Init(), "Mounting Blobfs");

    fbl::unique_ptr<blob_info_t> info;
    for (size_t i = 1; i < 18; i++) {
        ASSERT_TRUE(GenerateRandomBlob(1 << i, &info));

        char hexdigits[17] = "0123456789abcdef";
        size_t idx = strlen(info->path) - 1 - (rand() % (2 * Digest::kLength));
        char newchar = hexdigits[rand() % 16];
        while (info->path[idx] == newchar) {
            newchar = hexdigits[rand() % 16];
        }
        info->path[idx] = newchar;
        ASSERT_TRUE(MakeBlobCompromised(info.get()));
    }

    for (size_t i = 0; i < 18; i++) {
        ASSERT_TRUE(GenerateRandomBlob(1 << i, &info));
        // Flip a random bit of the data
        size_t rand_index = rand() % info->size_data;
        char old_val = info->data.get()[rand_index];
        while ((info->data.get()[rand_index] = static_cast<char>(rand())) == old_val) {
        }
        ASSERT_TRUE(MakeBlobCompromised(info.get()));
    }

    ASSERT_TRUE(blobfsTest.Teardown(), "unmounting blobfs");
    END_TEST;
}

template <FsTestType TestType>
static bool EdgeAllocation(void) {
    BEGIN_TEST;
    BlobfsTest blobfsTest(TestType);
    ASSERT_TRUE(blobfsTest.Init(), "Mounting Blobfs");

    // Powers of two...
    for (size_t i = 1; i < 16; i++) {
        // -1, 0, +1 offsets...
        for (size_t j = -1; j < 2; j++) {
            fbl::unique_ptr<blob_info_t> info;
            ASSERT_TRUE(GenerateRandomBlob((1 << i) + j, &info));
            fbl::unique_fd fd;
            ASSERT_TRUE(MakeBlob(info.get(), &fd));
            ASSERT_EQ(unlink(info->path), 0);
            ASSERT_EQ(close(fd.release()), 0);
        }
    }
    ASSERT_TRUE(blobfsTest.Teardown(), "unmounting blobfs");
    END_TEST;
}

template <FsTestType TestType>
static bool UmountWithOpenFile(void) {
    BEGIN_TEST;
    BlobfsTest blobfsTest(TestType);
    ASSERT_TRUE(blobfsTest.Init(), "Mounting Blobfs");

    fbl::unique_ptr<blob_info_t> info;
    ASSERT_TRUE(GenerateRandomBlob(1 << 16, &info));
    fbl::unique_fd fd;
    ASSERT_TRUE(MakeBlob(info.get(), &fd));

    // Intentionally don't close the file descriptor: Unmount anyway.
    ASSERT_TRUE(blobfsTest.Remount());
    // Just closing our local handle; the connection should be disconnected.
    ASSERT_EQ(close(fd.release()), -1);
    ASSERT_EQ(errno, EPIPE);

    fd.reset(open(info->path, O_RDONLY));
    ASSERT_TRUE(fd, "Failed to open blob");
    ASSERT_TRUE(VerifyContents(fd.get(), info->data.get(), info->size_data));
    ASSERT_EQ(close(fd.release()), 0, "Could not close blob");

    ASSERT_EQ(unlink(info->path), 0);
    ASSERT_TRUE(blobfsTest.Teardown(), "unmounting blobfs");
    END_TEST;
}

template <FsTestType TestType>
static bool UmountWithMappedFile(void) {
    BEGIN_TEST;
    BlobfsTest blobfsTest(TestType);
    ASSERT_TRUE(blobfsTest.Init(), "Mounting Blobfs");

    fbl::unique_ptr<blob_info_t> info;
    ASSERT_TRUE(GenerateRandomBlob(1 << 16, &info));
    fbl::unique_fd fd;
    ASSERT_TRUE(MakeBlob(info.get(), &fd));

    void* addr = mmap(nullptr, info->size_data, PROT_READ, MAP_SHARED, fd.get(), 0);
    ASSERT_NONNULL(addr);
    ASSERT_EQ(close(fd.release()), 0);

    // Intentionally don't unmap the file descriptor: Unmount anyway.
    ASSERT_TRUE(blobfsTest.Remount());
    // Just closing our local handle; the connection should be disconnected.
    ASSERT_EQ(munmap(addr, info->size_data), 0);

    fd.reset(open(info->path, O_RDONLY));
    ASSERT_GE(fd.get(), 0, "Failed to open blob");
    ASSERT_TRUE(VerifyContents(fd.get(), info->data.get(), info->size_data));
    ASSERT_EQ(close(fd.release()), 0, "Could not close blob");

    ASSERT_EQ(unlink(info->path), 0);
    ASSERT_TRUE(blobfsTest.Teardown(), "unmounting blobfs");
    END_TEST;
}

template <FsTestType TestType>
static bool UmountWithOpenMappedFile(void) {
    BEGIN_TEST;
    BlobfsTest blobfsTest(TestType);
    ASSERT_TRUE(blobfsTest.Init(), "Mounting Blobfs");

    fbl::unique_ptr<blob_info_t> info;
    ASSERT_TRUE(GenerateRandomBlob(1 << 16, &info));
    fbl::unique_fd fd;
    ASSERT_TRUE(MakeBlob(info.get(), &fd));

    void* addr = mmap(nullptr, info->size_data, PROT_READ, MAP_SHARED, fd.get(), 0);
    ASSERT_NONNULL(addr);

    // Intentionally don't close the file descriptor: Unmount anyway.
    ASSERT_TRUE(blobfsTest.Remount());
    // Just closing our local handle; the connection should be disconnected.
    ASSERT_EQ(munmap(addr, info->size_data), 0);
    ASSERT_EQ(close(fd.release()), -1);
    ASSERT_EQ(errno, EPIPE);

    fd.reset(open(info->path, O_RDONLY));
    ASSERT_GE(fd.get(), 0, "Failed to open blob");
    ASSERT_TRUE(VerifyContents(fd.get(), info->data.get(), info->size_data));
    ASSERT_EQ(close(fd.release()), 0, "Could not close blob");

    ASSERT_EQ(unlink(info->path), 0);
    ASSERT_TRUE(blobfsTest.Teardown(), "unmounting blobfs");
    END_TEST;
}

template <FsTestType TestType>
static bool CreateUmountRemountSmall(void) {
    BEGIN_TEST;
    BlobfsTest blobfsTest(TestType);
    ASSERT_TRUE(blobfsTest.Init(), "Mounting Blobfs");

    for (size_t i = 10; i < 16; i++) {
        fbl::unique_ptr<blob_info_t> info;
        ASSERT_TRUE(GenerateRandomBlob(1 << i, &info));

        fbl::unique_fd fd;
        ASSERT_TRUE(MakeBlob(info.get(), &fd));
        // Close fd, unmount filesystem
        ASSERT_EQ(close(fd.release()), 0);

        ASSERT_TRUE(blobfsTest.Remount(), "Could not re-mount blobfs");

        fd.reset(open(info->path, O_RDONLY));
        ASSERT_TRUE(fd, "Failed to open blob");

        ASSERT_TRUE(VerifyContents(fd.get(), info->data.get(), info->size_data));
        ASSERT_EQ(close(fd.release()), 0, "Could not close blob");
        ASSERT_EQ(unlink(info->path), 0);
    }

    ASSERT_TRUE(blobfsTest.Teardown(), "unmounting blobfs");
    END_TEST;
}

static bool check_not_readable(int fd) {
    struct pollfd fds;
    fds.fd = fd;
    fds.events = POLLIN;
    ASSERT_EQ(poll(&fds, 1, 10), 0, "Failed to wait for readable blob");

    char buf[8];
    ASSERT_EQ(lseek(fd, 0, SEEK_SET), 0);
    ASSERT_LT(read(fd, buf, 1), 0, "Blob should not be readable yet");
    return true;
}

static bool wait_readable(int fd) {
    struct pollfd fds;
    fds.fd = fd;
    fds.events = POLLIN;
    ASSERT_EQ(poll(&fds, 1, 10000), 1, "Failed to wait for readable blob");
    ASSERT_EQ(fds.revents, POLLIN);

    return true;
}

static bool check_readable(int fd) {
    struct pollfd fds;
    fds.fd = fd;
    fds.events = POLLIN;
    ASSERT_EQ(poll(&fds, 1, 10), 1, "Failed to wait for readable blob");
    ASSERT_EQ(fds.revents, POLLIN);

    char buf[8];
    ASSERT_EQ(lseek(fd, 0, SEEK_SET), 0);
    ASSERT_EQ(read(fd, buf, sizeof(buf)), sizeof(buf));
    return true;
}

template <FsTestType TestType>
static bool EarlyRead(void) {
    // Check that we cannot read from the Blob until it has been fully written
    BEGIN_TEST;
    BlobfsTest blobfsTest(TestType);
    ASSERT_TRUE(blobfsTest.Init(), "Mounting Blobfs");

    fbl::unique_ptr<blob_info_t> info;

    ASSERT_TRUE(GenerateRandomBlob(1 << 17, &info));
    fbl::unique_fd fd(open(info->path, O_CREAT | O_EXCL | O_RDWR));
    ASSERT_TRUE(fd, "Failed to create blob");

    ASSERT_LT(open(info->path, O_CREAT | O_EXCL | O_RDWR), 0,
              "Should not be able to exclusively create twice");

    // This second fd should also not be readable
    fbl::unique_fd fd2(open(info->path, O_CREAT | O_RDWR));
    ASSERT_TRUE(fd2, "Failed to create blob");

    ASSERT_TRUE(check_not_readable(fd.get()), "Should not be readable after open");
    ASSERT_TRUE(check_not_readable(fd2.get()), "Should not be readable after open");
    ASSERT_EQ(ftruncate(fd.get(), info->size_data), 0);
    ASSERT_TRUE(check_not_readable(fd.get()), "Should not be readable after alloc");
    ASSERT_TRUE(check_not_readable(fd2.get()), "Should not be readable after alloc");
    ASSERT_EQ(StreamAll(write, fd.get(), info->data.get(), info->size_data), 0,
              "Failed to write Data");

    // Okay, NOW we can read.
    // Double check that attempting to read early didn't cause problems...
    ASSERT_TRUE(VerifyContents(fd.get(), info->data.get(), info->size_data));
    ASSERT_TRUE(VerifyContents(fd2.get(), info->data.get(), info->size_data));

    // Cool, everything is readable. What if we try accessing the blob status now?
    EXPECT_TRUE(check_readable(fd.get()));

    ASSERT_EQ(close(fd.release()), 0);
    ASSERT_EQ(close(fd2.release()), 0);
    ASSERT_EQ(unlink(info->path), 0);

    ASSERT_TRUE(blobfsTest.Teardown(), "unmounting blobfs");
    END_TEST;
}

template <FsTestType TestType>
static bool WaitForRead(void) {
    // Check that we cannot read from the Blob until it has been fully written
    BEGIN_TEST;
    BlobfsTest blobfsTest(TestType);
    ASSERT_TRUE(blobfsTest.Init(), "Mounting Blobfs");

    fbl::unique_ptr<blob_info_t> info;

    ASSERT_TRUE(GenerateRandomBlob(1 << 17, &info));
    fbl::unique_fd fd(open(info->path, O_CREAT | O_EXCL | O_RDWR));
    ASSERT_TRUE(fd, "Failed to create blob");

    ASSERT_LT(open(info->path, O_CREAT | O_EXCL | O_RDWR), 0,
              "Should not be able to exclusively create twice");

    // Launch a background thread to wait for fd to become readable
    auto wait_until_readable = [](void* arg) {
        fbl::unique_fd fd(*static_cast<int*>(arg));
        EXPECT_TRUE(wait_readable(fd.get()));
        EXPECT_TRUE(check_readable(fd.get()));
        EXPECT_EQ(close(fd.release()), 0);
        return 0;
    };

    int dupfd = dup(fd.get());
    thrd_t waiter_thread;
    thrd_create(&waiter_thread, wait_until_readable, &dupfd);

    ASSERT_TRUE(check_not_readable(fd.get()), "Should not be readable after open");
    ASSERT_EQ(ftruncate(fd.get(), info->size_data), 0);
    ASSERT_TRUE(check_not_readable(fd.get()), "Should not be readable after alloc");
    ASSERT_EQ(StreamAll(write, fd.get(), info->data.get(), info->size_data), 0,
              "Failed to write Data");

    // Cool, everything is readable. What if we try accessing the blob status now?
    EXPECT_TRUE(check_readable(fd.get()));

    // Our background thread should have also completed successfully...
    int result;
    ASSERT_EQ(thrd_join(waiter_thread, &result), 0, "thrd_join failed");
    ASSERT_EQ(result, 0, "Unexpected result from background thread");

    // Double check that attempting to read early didn't cause problems...
    ASSERT_TRUE(VerifyContents(fd.get(), info->data.get(), info->size_data));
    ASSERT_EQ(close(fd.release()), 0);
    ASSERT_EQ(unlink(info->path), 0);

    ASSERT_TRUE(blobfsTest.Teardown(), "unmounting blobfs");
    END_TEST;
}

template <FsTestType TestType>
static bool WriteSeekIgnored(void) {
    // Check that seeks during writing are ignored
    BEGIN_TEST;
    BlobfsTest blobfsTest(TestType);
    ASSERT_TRUE(blobfsTest.Init(), "Mounting Blobfs");

    fbl::unique_ptr<blob_info_t> info;
    ASSERT_TRUE(GenerateRandomBlob(1 << 17, &info));
    fbl::unique_fd fd(open(info->path, O_CREAT | O_RDWR));
    ASSERT_TRUE(fd, "Failed to create blob");
    ASSERT_EQ(ftruncate(fd.get(), info->size_data), 0);

    size_t n = 0;
    while (n != info->size_data) {
        off_t seek_pos = (rand() % info->size_data);
        ASSERT_EQ(lseek(fd.get(), seek_pos, SEEK_SET), seek_pos);
        ssize_t d = write(fd.get(), info->data.get(), info->size_data - n);
        ASSERT_GT(d, 0, "Data Write error");
        n += d;
    }

    // Double check that attempting to seek early didn't cause problems...
    ASSERT_TRUE(VerifyContents(fd.get(), info->data.get(), info->size_data));
    ASSERT_EQ(close(fd.release()), 0);
    ASSERT_EQ(unlink(info->path), 0);

    ASSERT_TRUE(blobfsTest.Teardown(), "unmounting blobfs");
    END_TEST;
}

template <FsTestType TestType>
static bool UnlinkTiming(void) {
    // Try unlinking at a variety of times
    BEGIN_TEST;
    BlobfsTest blobfsTest(TestType);
    ASSERT_TRUE(blobfsTest.Init(), "Mounting Blobfs");

    // Unlink, close fd, re-open fd as new file
    auto full_unlink_reopen = [](fbl::unique_fd& fd, const char* path) {
        ASSERT_EQ(unlink(path), 0);
        ASSERT_EQ(close(fd.release()), 0);
        fd.reset(open(path, O_CREAT | O_RDWR | O_EXCL));
        ASSERT_TRUE(fd, "Failed to recreate blob");
        return true;
    };

    fbl::unique_ptr<blob_info_t> info;
    ASSERT_TRUE(GenerateRandomBlob(1 << 17, &info));

    fbl::unique_fd fd(open(info->path, O_CREAT | O_RDWR));
    ASSERT_TRUE(fd, "Failed to create blob");

    // Unlink after first open
    ASSERT_TRUE(full_unlink_reopen(fd, info->path));

    // Unlink after init
    ASSERT_EQ(ftruncate(fd.get(), info->size_data), 0);
    ASSERT_TRUE(full_unlink_reopen(fd, info->path));

    // Unlink after first write
    ASSERT_EQ(ftruncate(fd.get(), info->size_data), 0);
    ASSERT_EQ(StreamAll(write, fd.get(), info->data.get(), info->size_data), 0,
              "Failed to write Data");
    ASSERT_TRUE(full_unlink_reopen(fd, info->path));
    ASSERT_EQ(unlink(info->path), 0);
    ASSERT_EQ(close(fd.release()), 0);
    ASSERT_TRUE(blobfsTest.Teardown(), "unmounting blobfs");
    END_TEST;
}

template <FsTestType TestType>
static bool InvalidOps(void) {
    // Attempt using invalid operations
    BEGIN_TEST;
    BlobfsTest blobfsTest(TestType);
    ASSERT_TRUE(blobfsTest.Init(), "Mounting Blobfs");

    // First off, make a valid blob
    fbl::unique_ptr<blob_info_t> info;
    ASSERT_TRUE(GenerateRandomBlob(1 << 12, &info));
    fbl::unique_fd fd;
    ASSERT_TRUE(MakeBlob(info.get(), &fd));
    ASSERT_TRUE(VerifyContents(fd.get(), info->data.get(), info->size_data));

    // Neat. Now, let's try some unsupported operations
    ASSERT_LT(rename(info->path, info->path), 0);
    ASSERT_LT(truncate(info->path, 0), 0);
    ASSERT_LT(utime(info->path, nullptr), 0);

    // Test that a blob fd cannot unmount the entire blobfs.
    ASSERT_LT(ioctl_vfs_unmount_fs(fd.get()), 0);

    // Access the file once more, after these operations
    ASSERT_TRUE(VerifyContents(fd.get(), info->data.get(), info->size_data));
    ASSERT_EQ(unlink(info->path), 0);
    ASSERT_EQ(close(fd.release()), 0);

    ASSERT_TRUE(blobfsTest.Teardown(), "unmounting blobfs");
    END_TEST;
}

template <FsTestType TestType>
static bool RootDirectory(void) {
    // Attempt operations on the root directory
    BEGIN_TEST;
    BlobfsTest blobfsTest(TestType);
    ASSERT_TRUE(blobfsTest.Init(), "Mounting Blobfs");

    fbl::unique_fd dirfd(open(MOUNT_PATH "/.", O_RDONLY));
    ASSERT_TRUE(dirfd, "Cannot open root directory");

    fbl::unique_ptr<blob_info_t> info;
    ASSERT_TRUE(GenerateRandomBlob(1 << 12, &info));

    // Test ioctls which should ONLY operate on Blobs
    ASSERT_LT(ftruncate(dirfd.get(), info->size_data), 0);

    char buf[8];
    ASSERT_LT(write(dirfd.get(), buf, 8), 0, "Should not write to directory");
    ASSERT_LT(read(dirfd.get(), buf, 8), 0, "Should not read from directory");

    // Should NOT be able to unlink root dir
    ASSERT_EQ(close(dirfd.release()), 0);
    ASSERT_LT(unlink(info->path), 0);

    ASSERT_TRUE(blobfsTest.Teardown(), "unmounting blobfs");
    END_TEST;
}

template <FsTestType TestType>
bool TestPartialWrite(void) {
    BEGIN_TEST;
    BlobfsTest blobfsTest(TestType);
    ASSERT_TRUE(blobfsTest.Init(), "Mounting Blobfs");
    fbl::unique_ptr<blob_info_t> info_complete;
    fbl::unique_ptr<blob_info_t> info_partial;
    size_t size = 1 << 20;
    ASSERT_TRUE(GenerateRandomBlob(size, &info_complete));
    ASSERT_TRUE(GenerateRandomBlob(size, &info_partial));

    // Partially write out first blob.
    fbl::unique_fd fd_partial(open(info_partial->path, O_CREAT | O_RDWR));
    ASSERT_TRUE(fd_partial, "Failed to create blob");
    ASSERT_EQ(ftruncate(fd_partial.get(), size), 0);
    ASSERT_EQ(StreamAll(write, fd_partial.get(), info_partial->data.get(), size / 2), 0,
              "Failed to write Data");

    // Completely write out second blob.
    fbl::unique_fd fd_complete;
    ASSERT_TRUE(MakeBlob(info_complete.get(), &fd_complete));

    ASSERT_EQ(close(fd_complete.release()), 0);
    ASSERT_EQ(close(fd_partial.release()), 0);

    ASSERT_TRUE(blobfsTest.Teardown(), "unmounting blobfs");
    END_TEST;
}

template <FsTestType TestType>
bool TestPartialWriteSleepRamdisk(void) {
    BEGIN_TEST;
    if (gUseRealDisk) {
        fprintf(stderr, "Ramdisk required; skipping test\n");
        return true;
    }
    BlobfsTest blobfsTest(TestType);
    ASSERT_TRUE(blobfsTest.Init(), "Mounting Blobfs");
    fbl::unique_ptr<blob_info_t> info_complete;
    fbl::unique_ptr<blob_info_t> info_partial;
    size_t size = 1 << 20;
    ASSERT_TRUE(GenerateRandomBlob(size, &info_complete));
    ASSERT_TRUE(GenerateRandomBlob(size, &info_partial));

    // Partially write out first blob.
    fbl::unique_fd fd_partial(open(info_partial->path, O_CREAT | O_RDWR));
    ASSERT_TRUE(fd_partial, "Failed to create blob");
    ASSERT_EQ(ftruncate(fd_partial.get(), size), 0);
    ASSERT_EQ(StreamAll(write, fd_partial.get(), info_partial->data.get(), size / 2), 0,
              "Failed to write Data");

    // Completely write out second blob.
    fbl::unique_fd fd_complete;
    ASSERT_TRUE(MakeBlob(info_complete.get(), &fd_complete));

    ASSERT_EQ(syncfs(fd_complete.get()), 0);
    ASSERT_TRUE(blobfsTest.ToggleSleep());

    ASSERT_EQ(close(fd_complete.release()), 0);
    ASSERT_EQ(close(fd_partial.release()), 0);

    fd_complete.reset(open(info_complete->path, O_RDONLY));
    ASSERT_TRUE(fd_complete, "Failed to re-open blob");

    ASSERT_EQ(syncfs(fd_complete.get()), 0);
    ASSERT_TRUE(blobfsTest.ToggleSleep());

    ASSERT_TRUE(VerifyContents(fd_complete.get(), info_complete->data.get(), size));

    fd_partial.reset(open(info_partial->path, O_RDONLY));
    ASSERT_FALSE(fd_partial, "Should not be able to open invalid blob");
    ASSERT_EQ(close(fd_complete.release()), 0);

    ASSERT_TRUE(blobfsTest.Teardown(), "unmounting Blobfs");
    END_TEST;
}

enum TestState {
    empty,
    configured,
    readable,
};

typedef struct blob_state : public fbl::DoublyLinkedListable<fbl::unique_ptr<blob_state>> {
    blob_state(fbl::unique_ptr<blob_info_t> i)
        : info(fbl::move(i)), state(empty), writes_remaining(1) {
            bytes_remaining = info->size_data;
        }

    fbl::unique_ptr<blob_info_t> info;
    TestState state;
    fbl::unique_fd fd;
    size_t writes_remaining;
    size_t bytes_remaining;
} blob_state_t;

typedef struct blob_list {
    fbl::Mutex list_lock;
    fbl::DoublyLinkedList<fbl::unique_ptr<blob_state>> list;
    uint32_t blob_count = 0;
} blob_list_t;

// Make sure we do not exceed maximum fd count
static_assert(FDIO_MAX_FD >= 256, "");
constexpr uint32_t max_blobs = FDIO_MAX_FD - 32;

// Generate and open a new blob
bool blob_create_helper(blob_list_t* bl, unsigned* seed) {
    fbl::unique_ptr<blob_info_t> info;
    ASSERT_TRUE(GenerateRandomBlob(1 + (rand_r(seed) % (1 << 16)), &info));

    fbl::AllocChecker ac;
    fbl::unique_ptr<blob_state_t> state(new (&ac) blob_state(fbl::move(info)));
    ASSERT_EQ(ac.check(), true);

    {
        fbl::AutoLock al(&bl->list_lock);

        if (bl->blob_count >= max_blobs) {
            return true;
        }
        fbl::unique_fd fd(open(state->info->path, O_CREAT | O_RDWR));
        ASSERT_TRUE(fd, "Failed to create blob");
        state->fd.reset(fd.release());

        bl->list.push_front(fbl::move(state));
        bl->blob_count++;
    }
    return true;
}

// Allocate space for an open, empty blob
bool blob_config_helper(blob_list_t* bl) {
    fbl::unique_ptr<blob_state> state;
    {
        fbl::AutoLock al(&bl->list_lock);
        state = bl->list.pop_back();
    }

    if (state == nullptr) {
        return true;
    } else if (state->state == empty) {
        ASSERT_EQ(ftruncate(state->fd.get(), state->info->size_data), 0);
        state->state = configured;
    }
    {
        fbl::AutoLock al(&bl->list_lock);
        bl->list.push_front(fbl::move(state));
    }
    return true;
}

// Write the data for an open, partially written blob
bool blob_write_data_helper(blob_list_t* bl) {
    fbl::unique_ptr<blob_state> state;
    {
        fbl::AutoLock al(&bl->list_lock);
        state = bl->list.pop_back();
    }
    if (state == nullptr) {
        return true;
    } else if (state->state == configured) {
        size_t bytes_write = state->bytes_remaining / state->writes_remaining;
        size_t bytes_offset = state->info->size_data - state->bytes_remaining;
        ASSERT_EQ(StreamAll(write, state->fd.get(), state->info->data.get() + bytes_offset,
                            bytes_write), 0, "Failed to write Data");

        state->writes_remaining--;
        state->bytes_remaining -= bytes_write;
        if (state->writes_remaining == 0 && state->bytes_remaining == 0) {
            state->state = readable;
        }
    }
    {
        fbl::AutoLock al(&bl->list_lock);
        bl->list.push_front(fbl::move(state));
    }
    return true;
}

// Read the blob's data
bool blob_read_data_helper(blob_list_t* bl) {
    fbl::unique_ptr<blob_state> state;
    {
        fbl::AutoLock al(&bl->list_lock);
        state = bl->list.pop_back();
    }
    if (state == nullptr) {
        return true;
    } else if (state->state == readable) {
        ASSERT_TRUE(VerifyContents(state->fd.get(), state->info->data.get(),
                                   state->info->size_data));
    }
    {
        fbl::AutoLock al(&bl->list_lock);
        bl->list.push_front(fbl::move(state));
    }
    return true;
}

// Unlink the blob
bool blob_unlink_helper(blob_list_t* bl) {
    fbl::unique_ptr<blob_state> state;
    {
        fbl::AutoLock al(&bl->list_lock);
        state = bl->list.pop_back();
    }
    if (state == nullptr) {
        return true;
    }
    ASSERT_EQ(unlink(state->info->path), 0, "Could not unlink blob");
    ASSERT_EQ(close(state->fd.release()), 0, "Could not close blob");
    {
        fbl::AutoLock al(&bl->list_lock);
        bl->blob_count--;
    }
    return true;
}

bool blob_reopen_helper(blob_list_t* bl) {
    fbl::unique_ptr<blob_state> state;
    {
        fbl::AutoLock al(&bl->list_lock);
        state = bl->list.pop_back();
    }
    if (state == nullptr) {
        return true;
    } else if (state->state == readable) {
        ASSERT_EQ(close(state->fd.release()), 0, "Could not close blob");
        fbl::unique_fd fd(open(state->info->path, O_RDONLY));
        ASSERT_TRUE(fd, "Failed to reopen blob");
        state->fd.reset(fd.release());
    }
    {
        fbl::AutoLock al(&bl->list_lock);
        bl->list.push_front(fbl::move(state));
    }
    return true;
}

template <FsTestType TestType>
bool TestAlternateWrite(void) {
    BEGIN_TEST;
    BlobfsTest blobfsTest(TestType);
    ASSERT_TRUE(blobfsTest.Init(), "Mounting Blobfs");
    size_t num_blobs = 1;
    size_t num_writes = 100;
    unsigned int seed = static_cast<unsigned int>(zx_ticks_get());
    blob_list_t bl;

    for (size_t i = 0; i < num_blobs; i++) {
        ASSERT_TRUE(blob_create_helper(&bl, &seed));
        bl.list.front().writes_remaining = num_writes;
    }

    for (size_t i = 0; i < num_blobs; i++) {
        ASSERT_TRUE(blob_config_helper(&bl));
    }

    for (size_t i = 0; i < num_writes; i++) {
        for (size_t j = 0; j < num_blobs; j++) {
            ASSERT_TRUE(blob_write_data_helper(&bl));
        }
    }

    for (size_t i = 0; i < num_blobs; i++) {
        ASSERT_TRUE(blob_reopen_helper(&bl));
    }

    for (auto& state : bl.list) {
        ASSERT_TRUE(check_readable(state.fd.get()));
    }

    for (size_t i = 0; i < num_blobs; i++) {
        ASSERT_TRUE(blob_read_data_helper(&bl));
    }

    for (auto& state : bl.list) {
        ASSERT_EQ(close(state.fd.release()), 0);
    }
    ASSERT_TRUE(blobfsTest.Teardown(), "Unmounting Blobfs");
    END_TEST;
}

template <FsTestType TestType>
static bool TestHugeBlobRandom(void) {
    BEGIN_TEST;
    BlobfsTest blobfsTest(TestType);
    ASSERT_TRUE(blobfsTest.Init(), "Mounting Blobfs");
    fbl::unique_ptr<blob_info_t> info;

    // This blob is extremely large, and will remain large
    // on disk. It is not easily compressible.
    ASSERT_TRUE(GenerateRandomBlob(2 * blobfs::kWriteBufferBytes, &info));

    fbl::unique_fd fd;
    ASSERT_TRUE(MakeBlob(info.get(), &fd));
    ASSERT_EQ(close(fd.release()), 0);

    // We can re-open and verify the Blob as read-only
    fd.reset(open(info->path, O_RDONLY));
    ASSERT_TRUE(fd, "Failed to-reopen blob");
    ASSERT_TRUE(VerifyContents(fd.get(), info->data.get(), info->size_data));
    ASSERT_EQ(close(fd.release()), 0);

    // We cannot re-open the blob as writable
    fd.reset(open(info->path, O_RDWR | O_CREAT));
    ASSERT_FALSE(fd, "Shouldn't be able to re-create blob that exists");
    fd.reset(open(info->path, O_RDWR));
    ASSERT_FALSE(fd, "Shouldn't be able to re-open blob as writable");
    fd.reset(open(info->path, O_WRONLY));
    ASSERT_FALSE(fd, "Shouldn't be able to re-open blob as writable");

    // Force decompression by remounting, re-accessing blob.
    ASSERT_TRUE(blobfsTest.Remount());
    fd.reset(open(info->path, O_RDONLY));
    ASSERT_TRUE(fd, "Failed to-reopen blob");
    ASSERT_TRUE(VerifyContents(fd.get(), info->data.get(), info->size_data));
    ASSERT_EQ(close(fd.release()), 0);

    ASSERT_EQ(unlink(info->path), 0);

    ASSERT_TRUE(blobfsTest.Teardown(), "Mounting Blobfs");
    END_TEST;
}

template <FsTestType TestType>
static bool TestHugeBlobCompressible(void) {
    BEGIN_TEST;
    BlobfsTest blobfsTest(TestType);
    ASSERT_TRUE(blobfsTest.Init(), "Mounting Blobfs");
    fbl::unique_ptr<blob_info_t> info;

    // This blob is extremely large, and will remain large
    // on disk, even though is very compressible.
    ASSERT_TRUE(GenerateBlob([](char* data, size_t length) {
        RandomFill(data, length / 2);
        data = reinterpret_cast<char*>(reinterpret_cast<uintptr_t>(data) + length / 2);
        memset(data, 'a', length / 2);
    }, 2 * blobfs::kWriteBufferBytes, &info));

    fbl::unique_fd fd;
    ASSERT_TRUE(MakeBlob(info.get(), &fd));
    ASSERT_EQ(close(fd.release()), 0);

    // We can re-open and verify the Blob as read-only
    fd.reset(open(info->path, O_RDONLY));
    ASSERT_TRUE(fd, "Failed to-reopen blob");
    ASSERT_TRUE(VerifyContents(fd.get(), info->data.get(), info->size_data));
    ASSERT_EQ(close(fd.release()), 0);

    // We cannot re-open the blob as writable
    fd.reset(open(info->path, O_RDWR | O_CREAT));
    ASSERT_FALSE(fd, "Shouldn't be able to re-create blob that exists");
    fd.reset(open(info->path, O_RDWR));
    ASSERT_FALSE(fd, "Shouldn't be able to re-open blob as writable");
    fd.reset(open(info->path, O_WRONLY));
    ASSERT_FALSE(fd, "Shouldn't be able to re-open blob as writable");

    // Force decompression by remounting, re-accessing blob.
    ASSERT_TRUE(blobfsTest.Remount());
    fd.reset(open(info->path, O_RDONLY));
    ASSERT_TRUE(fd, "Failed to-reopen blob");
    ASSERT_TRUE(VerifyContents(fd.get(), info->data.get(), info->size_data));
    ASSERT_EQ(close(fd.release()), 0);

    ASSERT_EQ(unlink(info->path), 0);

    ASSERT_TRUE(blobfsTest.Teardown(), "Mounting Blobfs");
    END_TEST;
}

template <FsTestType TestType>
static bool CreateUmountRemountLarge(void) {
    BEGIN_TEST;
    BlobfsTest blobfsTest(TestType);
    ASSERT_TRUE(blobfsTest.Init(), "Mounting Blobfs");

    blob_list_t bl;
    // TODO(smklein): Here, and elsewhere in this file, remove this source
    // of randomness to make the unit test deterministic -- fuzzing should
    // be the tool responsible for introducing randomness into the system.
    unsigned int seed = static_cast<unsigned int>(zx_ticks_get());
    unittest_printf("unmount_remount test using seed: %u\n", seed);

    // Do some operations...
    size_t num_ops = 5000;
    for (size_t i = 0; i < num_ops; ++i) {
        switch (rand_r(&seed) % 6) {
        case 0:
            ASSERT_TRUE(blob_create_helper(&bl, &seed));
            break;
        case 1:
            ASSERT_TRUE(blob_config_helper(&bl));
            break;
        case 2:
            ASSERT_TRUE(blob_write_data_helper(&bl));
            break;
        case 3:
            ASSERT_TRUE(blob_read_data_helper(&bl));
            break;
        case 4:
            ASSERT_TRUE(blob_reopen_helper(&bl));
            break;
        case 5:
            ASSERT_TRUE(blob_unlink_helper(&bl));
            break;
        }
    }

    // Close all currently opened nodes (REGARDLESS of their state)
    for (auto& state : bl.list) {
        ASSERT_EQ(close(state.fd.release()), 0);
    }

    // Unmount, remount
    ASSERT_TRUE(blobfsTest.Remount(), "Could not re-mount blobfs");

    for (auto& state : bl.list) {
        if (state.state == readable) {
            // If a blob was readable before being unmounted, it should still exist.
            fbl::unique_fd fd(open(state.info->path, O_RDONLY));
            ASSERT_TRUE(fd, "Failed to create blob");
            ASSERT_TRUE(VerifyContents(fd.get(), state.info->data.get(),
                                       state.info->size_data));
            ASSERT_EQ(unlink(state.info->path), 0);
            ASSERT_EQ(close(fd.release()), 0);
        } else {
            // ... otherwise, the blob should have been deleted.
            ASSERT_LT(open(state.info->path, O_RDONLY), 0);
        }
    }

    ASSERT_TRUE(blobfsTest.Teardown(), "unmounting blobfs");
    END_TEST;
}

int unmount_remount_thread(void* arg) {
    blob_list_t* bl = static_cast<blob_list_t*>(arg);
    unsigned int seed = static_cast<unsigned int>(zx_ticks_get());
    unittest_printf("unmount_remount thread using seed: %u\n", seed);

    // Do some operations...
    size_t num_ops = 1000;
    for (size_t i = 0; i < num_ops; ++i) {
        switch (rand_r(&seed) % 6) {
        case 0:
            ASSERT_TRUE(blob_create_helper(bl, &seed));
            break;
        case 1:
            ASSERT_TRUE(blob_config_helper(bl));
            break;
        case 2:
            ASSERT_TRUE(blob_write_data_helper(bl));
            break;
        case 3:
            ASSERT_TRUE(blob_read_data_helper(bl));
            break;
        case 4:
            ASSERT_TRUE(blob_reopen_helper(bl));
            break;
        case 5:
            ASSERT_TRUE(blob_unlink_helper(bl));
            break;
        }
    }

    return 0;
}

template <FsTestType TestType>
static bool CreateUmountRemountLargeMultithreaded(void) {
    BEGIN_TEST;
    BlobfsTest blobfsTest(TestType);
    ASSERT_TRUE(blobfsTest.Init(), "Mounting Blobfs");

    blob_list_t bl;

    size_t num_threads = 10;
    fbl::AllocChecker ac;
    fbl::Array<thrd_t> threads(new (&ac) thrd_t[num_threads](), num_threads);
    ASSERT_TRUE(ac.check());

    // Launch all threads
    for (size_t i = 0; i < num_threads; i++) {
        ASSERT_EQ(thrd_create(&threads[i], unmount_remount_thread, &bl),
                  thrd_success);
    }

    // Wait for all threads to complete
    for (size_t i = 0; i < num_threads; i++) {
        int res;
        ASSERT_EQ(thrd_join(threads[i], &res), thrd_success);
        ASSERT_EQ(res, 0);
    }

    // Close all currently opened nodes (REGARDLESS of their state)
    for (auto& state : bl.list) {
        ASSERT_EQ(close(state.fd.release()), 0);
    }

    // Unmount, remount
    ASSERT_TRUE(blobfsTest.Remount(), "Could not re-mount blobfs");

    for (auto& state : bl.list) {
        if (state.state == readable) {
            // If a blob was readable before being unmounted, it should still exist.
            fbl::unique_fd fd(open(state.info->path, O_RDONLY));
            ASSERT_TRUE(fd, "Failed to create blob");
            ASSERT_TRUE(VerifyContents(fd.get(), state.info->data.get(),
                                       state.info->size_data));
            ASSERT_EQ(unlink(state.info->path), 0);
            ASSERT_EQ(close(fd.release()), 0);
        } else {
            // ... otherwise, the blob should have been deleted.
            ASSERT_LT(open(state.info->path, O_RDONLY), 0);
        }
    }

    ASSERT_TRUE(blobfsTest.Teardown(), "unmounting blobfs");
    END_TEST;
}


template <FsTestType TestType>
static bool NoSpace(void) {
    BEGIN_TEST;
    BlobfsTest blobfsTest(TestType);
    ASSERT_TRUE(blobfsTest.Init(), "Mounting Blobfs");

    fbl::unique_ptr<blob_info_t> last_info = nullptr;

    // Keep generating blobs until we run out of space
    size_t count = 0;
    while (true) {
        fbl::unique_ptr<blob_info_t> info;
        ASSERT_TRUE(GenerateRandomBlob(1 << 17, &info));

        fbl::unique_fd fd(open(info->path, O_CREAT | O_RDWR));
        ASSERT_TRUE(fd, "Failed to create blob");
        int r = ftruncate(fd.get(), info->size_data);
        if (r < 0) {
            ASSERT_EQ(errno, ENOSPC, "Blobfs expected to run out of space");
            // We ran out of space, as expected. Can we allocate if we
            // unlink a previously allocated blob of the desired size?
            ASSERT_EQ(unlink(last_info->path), 0, "Unlinking old blob");
            ASSERT_EQ(ftruncate(fd.get(), info->size_data), 0, "Re-init after unlink");

            // Yay! allocated successfully.
            ASSERT_EQ(close(fd.release()), 0);
            break;
        }
        ASSERT_EQ(StreamAll(write, fd.get(), info->data.get(), info->size_data), 0,
                  "Failed to write Data");
        ASSERT_EQ(close(fd.release()), 0);
        last_info = fbl::move(info);

        if (++count % 50 == 0) {
            printf("Allocated %lu blobs\n", count);
        }
    }

    ASSERT_TRUE(blobfsTest.Teardown(), "unmounting blobfs");
    END_TEST;
}

template <FsTestType TestType>
static bool QueryDevicePath(void) {
    BEGIN_TEST;
    BlobfsTest blobfsTest(TestType);
    ASSERT_TRUE(blobfsTest.Init(), "Mounting Blobfs");

    fbl::unique_fd dirfd(open(MOUNT_PATH "/.", O_RDONLY | O_ADMIN));
    ASSERT_TRUE(dirfd, "Cannot open root directory");

    char device_path[1024];
    ssize_t path_len = ioctl_vfs_get_device_path(dirfd.get(), device_path, sizeof(device_path));
    ASSERT_GT(path_len, 0, "Device path not found");

    char actual_path[PATH_MAX];
    ASSERT_TRUE(blobfsTest.GetDevicePath(actual_path, PATH_MAX));
    ASSERT_EQ(strncmp(actual_path, device_path, path_len), 0, "Unexpected device path");
    ASSERT_EQ(close(dirfd.release()), 0);

    dirfd.reset(open(MOUNT_PATH "/.", O_RDONLY));
    ASSERT_TRUE(dirfd, "Cannot open root directory");
    path_len = ioctl_vfs_get_device_path(dirfd.get(), device_path, sizeof(device_path));
    ASSERT_LT(path_len, 0);
    ASSERT_EQ(close(dirfd.release()), 0);

    ASSERT_TRUE(blobfsTest.Teardown(), "unmounting blobfs");
    END_TEST;
}

template <FsTestType TestType>
static bool TestReadOnly(void) {
    BEGIN_TEST;
    BlobfsTest blobfsTest(TestType);
    ASSERT_TRUE(blobfsTest.Init(), "Mounting Blobfs");

    // Mount the filesystem as read-write.
    // We can create new blobs.
    fbl::unique_ptr<blob_info_t> info;
    ASSERT_TRUE(GenerateRandomBlob(1 << 10, &info));
    fbl::unique_fd blob_fd;
    ASSERT_TRUE(MakeBlob(info.get(), &blob_fd));
    ASSERT_TRUE(VerifyContents(blob_fd.get(), info->data.get(), info->size_data));
    ASSERT_EQ(close(blob_fd.release()), 0);

    blobfsTest.SetReadOnly(true);
    ASSERT_TRUE(blobfsTest.Remount());

    // We can read old blobs
    blob_fd.reset(open(info->path, O_RDONLY));
    ASSERT_GE(blob_fd.get(), 0);
    ASSERT_TRUE(VerifyContents(blob_fd.get(), info->data.get(), info->size_data));
    ASSERT_EQ(close(blob_fd.release()), 0);

    // We cannot create new blobs
    ASSERT_TRUE(GenerateRandomBlob(1 << 10, &info));
    ASSERT_LT(open(info->path, O_CREAT | O_RDWR), 0);

    ASSERT_TRUE(blobfsTest.Teardown(), "unmounting blobfs");
    END_TEST;
}

// This tests growing both additional inodes and blocks
template <FsTestType TestType>
static bool ResizePartition(void) {
    BEGIN_TEST;
    ASSERT_EQ(TestType, FsTestType::kFvm);
    BlobfsTest blobfsTest(TestType);
    ASSERT_TRUE(blobfsTest.Init(), "Mounting Blobfs");

    // Create 1000 blobs. Test slices are small enough that this will require both inodes and
    // blocks to be added
    for (size_t d = 0; d < 1000; d++) {
        if (d % 100 == 0) {
            printf("Creating blob: %lu\n", d);
        }

        fbl::unique_ptr<blob_info_t> info;
        ASSERT_TRUE(GenerateRandomBlob(64, &info));

        fbl::unique_fd fd;
        ASSERT_TRUE(MakeBlob(info.get(), &fd));
        ASSERT_EQ(close(fd.release()), 0);
    }

    printf("Remounting blobfs\n");
    // Remount partition
    ASSERT_TRUE(blobfsTest.Remount(), "Could not re-mount blobfs");

    DIR* dir = opendir(MOUNT_PATH);
    ASSERT_NONNULL(dir);
    unsigned entries_deleted = 0;
    char path[PATH_MAX];
    struct dirent* de;

    // Unlink all blobs
    while ((de = readdir(dir)) != nullptr) {
        if (entries_deleted % 100 == 0) {
            printf("Unlinking blob: %u\n", entries_deleted);
        }
        strcpy(path, MOUNT_PATH "/");
        strcat(path, de->d_name);
        ASSERT_EQ(unlink(path), 0);
        entries_deleted++;
    }

    printf("Completing test\n");
    ASSERT_EQ(closedir(dir), 0);
    ASSERT_EQ(entries_deleted, 1000);
    ASSERT_TRUE(blobfsTest.Teardown(), "unmounting blobfs");
    END_TEST;
}

template <FsTestType TestType>
static bool CorruptAtMount(void) {
    BEGIN_TEST;
    ASSERT_EQ(TestType, FsTestType::kFvm);

    BlobfsTest blobfsTest(TestType);
    ASSERT_TRUE(blobfsTest.Init(), "Mounting Blobfs");

    ASSERT_EQ(umount(MOUNT_PATH), ZX_OK, "Could not unmount blobfs");

    fbl::unique_fd fd(blobfsTest.GetFd());
    ASSERT_TRUE(fd, "Could not open ramdisk");

    // Manually shrink slice so FVM will differ from Blobfs.
    extend_request_t extend_request;
    extend_request.offset = blobfs::kFVMNodeMapStart / kBlocksPerSlice;
    extend_request.length = 1;
    ASSERT_EQ(ioctl_block_fvm_shrink(fd.get(), &extend_request), 0);

    // Verify that shrink was successful.
    query_request_t query_request;
    query_request.count = 1;
    query_request.vslice_start[0] = extend_request.offset;
    query_response_t query_response;
    ASSERT_EQ(ioctl_block_fvm_vslice_query(fd.get(), &query_request, &query_response),
              sizeof(query_response_t));
    ASSERT_EQ(query_request.count, query_response.count);
    ASSERT_FALSE(query_response.vslice_range[0].allocated);
    ASSERT_EQ(query_response.vslice_range[0].count,
              (blobfs::kFVMDataStart - blobfs::kFVMNodeMapStart) / kBlocksPerSlice);

    // Attempt to mount the VPart. This should fail since slices are missing.
    ASSERT_NE(mount(fd.release(), MOUNT_PATH, DISK_FORMAT_BLOBFS, &default_mount_options,
                    launch_stdio_async), ZX_OK);

    fd.reset(blobfsTest.GetFd());
    ASSERT_TRUE(fd, "Could not open ramdisk");

    // Manually grow slice count to twice what it was initially.
    extend_request.length = 2;
    ASSERT_EQ(ioctl_block_fvm_extend(fd.get(), &extend_request), 0);

    // Verify that extend was successful.
    ASSERT_EQ(ioctl_block_fvm_vslice_query(fd.get(), &query_request, &query_response),
              sizeof(query_response_t));
    ASSERT_EQ(query_request.count, query_response.count);
    ASSERT_TRUE(query_response.vslice_range[0].allocated);
    ASSERT_EQ(query_response.vslice_range[0].count, 2);

    // Attempt to mount the VPart. This should succeed.
    ASSERT_EQ(mount(fd.release(), MOUNT_PATH, DISK_FORMAT_BLOBFS, &default_mount_options,
                    launch_stdio_async), ZX_OK);

    ASSERT_EQ(umount(MOUNT_PATH), ZX_OK);
    fd.reset(blobfsTest.GetFd());
    ASSERT_TRUE(fd, "Could not open ramdisk");

    // Verify that mount automatically removed extra slice.
    ASSERT_EQ(ioctl_block_fvm_vslice_query(fd.get(), &query_request, &query_response),
              sizeof(query_response_t));
    ASSERT_EQ(query_request.count, query_response.count);
    ASSERT_TRUE(query_response.vslice_range[0].allocated);
    ASSERT_EQ(query_response.vslice_range[0].count, 1);

    // Clean up.
    ASSERT_TRUE(blobfsTest.Teardown(FsTestState::kMinimal), "unmounting Blobfs");
    END_TEST;
}

typedef struct reopen_data {
    char path[PATH_MAX];
    fbl::atomic_bool complete;
} reopen_data_t;

int reopen_thread(void* arg) {
    reopen_data_t* dat = static_cast<reopen_data_t*>(arg);
    unsigned attempts = 0;
    while (!atomic_load(&dat->complete)) {
        fbl::unique_fd fd(open(dat->path, O_RDONLY));
        ASSERT_TRUE(fd);
        ASSERT_EQ(close(fd.release()), 0);
        attempts++;
    }

    printf("Reopened %u times\n", attempts);
    return 0;
}

// The purpose of this test is to repro the case where a blob is being retrieved from the blob hash
// at the same time it is being destructed, causing an invalid vnode to be returned. This can only
// occur when the client is opening a new fd to the blob at the same time it is being destructed
// after all writes to disk have completed.
// This test works best if a sleep is added at the beginning of fbl_recycle in VnodeBlob.
template <FsTestType TestType>
static bool CreateWriteReopen(void) {
    BEGIN_TEST;
    BlobfsTest blobfsTest(TestType);
    ASSERT_TRUE(blobfsTest.Init(), "Mounting Blobfs");

    size_t num_ops = 10;

    fbl::unique_ptr<blob_info_t> anchor_info;
    ASSERT_TRUE(GenerateRandomBlob(1 << 10, &anchor_info));

    fbl::unique_ptr<blob_info_t> info;
    ASSERT_TRUE(GenerateRandomBlob(10 *(1 << 20), &info));
    reopen_data_t dat;
    strcpy(dat.path, info->path);

    for (size_t i = 0; i < num_ops; i++) {
        printf("Running op %lu... ", i);
        fbl::unique_fd fd;
        fbl::unique_fd anchor_fd;
        atomic_store(&dat.complete, false);

        // Write both blobs to disk (without verification, so we can start reopening the blob asap)
        ASSERT_TRUE(MakeBlobUnverified(info.get(), &fd));
        ASSERT_TRUE(MakeBlobUnverified(anchor_info.get(), &anchor_fd));
        ASSERT_EQ(close(fd.release()), 0);

        thrd_t thread;
        ASSERT_EQ(thrd_create(&thread, reopen_thread, &dat), thrd_success);

        // Sleep while the thread continually opens and closes the blob
        usleep(1000000);
        ASSERT_EQ(syncfs(anchor_fd.get()), 0);
        atomic_store(&dat.complete, true);

        int res;
        ASSERT_EQ(thrd_join(thread, &res), thrd_success);
        ASSERT_EQ(res, 0);

        ASSERT_EQ(close(anchor_fd.release()), 0);
        ASSERT_EQ(unlink(info->path), 0);
        ASSERT_EQ(unlink(anchor_info->path), 0);
    }

    ASSERT_TRUE(blobfsTest.Teardown(), "unmounting Blobfs");
    END_TEST;
}

// Ensure Compressor returns an error if we try to compress more data than the buffer can hold.
static bool TestCompressorBufferTooSmall(void) {
    BEGIN_TEST;
    blobfs::Compressor c;

    // Pretend we're going to compress only one byte of data.
    const size_t buf_size = c.BufferMax(1);
    fbl::AllocChecker ac;
    fbl::unique_ptr<char[]> buf(new (&ac) char[buf_size]);
    EXPECT_EQ(ac.check(), true);
    ASSERT_EQ(c.Initialize(buf.get(), buf_size), ZX_OK);

    // Keep compressing data until Compressor returns an error.
    unsigned int seed = 0;
    zx_status_t result = ZX_OK;
    for (;;) {
        char data = static_cast<char>(rand_r(&seed));
        result = c.Update(&data, 1);
        if (result != ZX_OK) {
            break;
        }
    }
    ASSERT_EQ(result, ZX_ERR_IO_DATA_INTEGRITY);

    END_TEST;
}

BEGIN_TEST_CASE(blobfs_tests)
RUN_TEST_FOR_ALL_TYPES(MEDIUM, TestBasic)
RUN_TEST_FOR_ALL_TYPES(MEDIUM, TestNullBlob)
RUN_TEST_FOR_ALL_TYPES(MEDIUM, TestCompressibleBlob)
RUN_TEST_FOR_ALL_TYPES(MEDIUM, TestMmap)
RUN_TEST_FOR_ALL_TYPES(MEDIUM, TestMmapUseAfterClose)
RUN_TEST_FOR_ALL_TYPES(MEDIUM, TestReaddir)
RUN_TEST_FOR_ALL_TYPES(MEDIUM, TestDiskTooSmall)
RUN_TEST_MEDIUM(TestQueryInfo<FsTestType::kFvm>)
RUN_TEST_FOR_ALL_TYPES(MEDIUM, UseAfterUnlink)
RUN_TEST_FOR_ALL_TYPES(MEDIUM, WriteAfterRead)
RUN_TEST_FOR_ALL_TYPES(MEDIUM, WriteAfterUnlink)
RUN_TEST_FOR_ALL_TYPES(MEDIUM, ReadTooLarge)
RUN_TEST_FOR_ALL_TYPES(MEDIUM, BadAllocation)
RUN_TEST_FOR_ALL_TYPES(MEDIUM, CorruptedBlob)
RUN_TEST_FOR_ALL_TYPES(MEDIUM, CorruptedDigest)
RUN_TEST_FOR_ALL_TYPES(MEDIUM, EdgeAllocation)
RUN_TEST_FOR_ALL_TYPES(MEDIUM, UmountWithOpenFile)
RUN_TEST_FOR_ALL_TYPES(MEDIUM, UmountWithMappedFile)
RUN_TEST_FOR_ALL_TYPES(MEDIUM, UmountWithOpenMappedFile)
RUN_TEST_FOR_ALL_TYPES(MEDIUM, CreateUmountRemountSmall)
RUN_TEST_FOR_ALL_TYPES(MEDIUM, EarlyRead)
RUN_TEST_FOR_ALL_TYPES(MEDIUM, WaitForRead)
RUN_TEST_FOR_ALL_TYPES(MEDIUM, WriteSeekIgnored)
RUN_TEST_FOR_ALL_TYPES(MEDIUM, UnlinkTiming)
RUN_TEST_FOR_ALL_TYPES(MEDIUM, InvalidOps)
RUN_TEST_FOR_ALL_TYPES(MEDIUM, RootDirectory)
RUN_TEST_FOR_ALL_TYPES(MEDIUM, TestPartialWrite)
RUN_TEST_FOR_ALL_TYPES(MEDIUM, TestPartialWriteSleepRamdisk)
RUN_TEST_FOR_ALL_TYPES(MEDIUM, TestAlternateWrite)
RUN_TEST_FOR_ALL_TYPES(LARGE, TestHugeBlobRandom)
RUN_TEST_FOR_ALL_TYPES(LARGE, TestHugeBlobCompressible)
RUN_TEST_FOR_ALL_TYPES(LARGE, CreateUmountRemountLarge)
RUN_TEST_FOR_ALL_TYPES(LARGE, CreateUmountRemountLargeMultithreaded)
RUN_TEST_FOR_ALL_TYPES(LARGE, NoSpace)
RUN_TEST_FOR_ALL_TYPES(MEDIUM, QueryDevicePath)
RUN_TEST_FOR_ALL_TYPES(MEDIUM, TestReadOnly)
RUN_TEST_MEDIUM(ResizePartition<FsTestType::kFvm>)
RUN_TEST_MEDIUM(CorruptAtMount<FsTestType::kFvm>)
RUN_TEST_FOR_ALL_TYPES(LARGE, CreateWriteReopen)
RUN_TEST(TestCompressorBufferTooSmall);
END_TEST_CASE(blobfs_tests)

static void print_test_help(FILE* f) {
    fprintf(f,
            "  -d <blkdev>\n"
            "      Use block device <blkdev> instead of a ramdisk\n"
            "\n");
}

int main(int argc, char** argv) {
    unittest_register_test_help_printer(print_test_help);

    int i = 1;
    while (i < argc) {
        if (!strcmp(argv[i], "-d") && (i + 1 < argc)) {
            fbl::unique_fd fd(open(argv[i + 1], O_RDWR));
            if (!fd) {
                fprintf(stderr, "[fs] Could not open block device\n");
                return -1;
            } else if (ioctl_device_get_topo_path(fd.get(), gRealDiskInfo.disk_path, PATH_MAX)
                       < 0) {
                fprintf(stderr, "[fs] Could not acquire topological path of block device\n");
                return -1;
            }

            // If we previously tried running tests on this disk, it may
            // have created an FVM and failed. (Try to) clean up from previous state
            // before re-running.
            fvm_destroy(gRealDiskInfo.disk_path);

            block_info_t block_info;
            ssize_t rc = ioctl_block_get_info(fd.get(), &block_info);

            if (rc < 0 || rc != sizeof(block_info)) {
                fprintf(stderr, "[fs] Could not query block device info\n");
                return -1;
            }

            gUseRealDisk = true;
            gRealDiskInfo.blk_size = block_info.block_size;
            gRealDiskInfo.blk_count = block_info.block_count;

            size_t disk_size = gRealDiskInfo.blk_size * gRealDiskInfo.blk_count;
            if (disk_size < kBytesNormalMinimum) {
                fprintf(stderr, "Error: Insufficient disk space for tests");
                return -1;
            } else if (disk_size < kTotalBytesFvmMinimum) {
                fprintf(stderr, "Error: Insufficient disk space for FVM tests");
                return -1;
            }
            i += 2;
        } else {
            // Ignore options we don't recognize. See ulib/unittest/README.md.
            break;
        }
    }

    // Initialize tmpfs.
    async::Loop loop;
    if (loop.StartThread() != ZX_OK) {
        fprintf(stderr, "Error: Cannot initialize local tmpfs loop\n");
        return -1;
    }
    if (memfs_install_at(loop.dispatcher(), TMPFS_PATH) != ZX_OK) {
        fprintf(stderr, "Error: Cannot install local tmpfs\n");
        return -1;
    }

    return unittest_run_all_tests(argc, argv) ? 0 : -1;
}
