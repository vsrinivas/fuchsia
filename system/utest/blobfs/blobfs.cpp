// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <threads.h>
#include <utime.h>

#include <blobfs/format.h>
#include <digest/digest.h>
#include <digest/merkle-tree.h>
#include <fdio/io.h>
#include <fbl/atomic.h>
#include <fbl/alloc_checker.h>
#include <fbl/auto_call.h>
#include <fbl/auto_lock.h>
#include <fbl/intrusive_double_list.h>
#include <fbl/unique_fd.h>
#include <fbl/unique_ptr.h>
#include <fs-management/mount.h>
#include <fs-management/ramdisk.h>
#include <fvm/fvm.h>
#include <zircon/device/device.h>
#include <zircon/device/ramdisk.h>
#include <zircon/device/vfs.h>
#include <zircon/syscalls.h>
#include <unittest/unittest.h>

#include "blobfs-test.h"

#define MOUNT_PATH "/tmp/zircon-blobfs-test"

namespace {
using digest::Digest;
using digest::MerkleTree;

#define RUN_TEST_FOR_ALL_TYPES(test_size, test_name) \
    RUN_TEST_##test_size(test_name<FsTestType::kNormal>)  \
    RUN_TEST_##test_size(test_name<FsTestType::kFvm>)

#define FVM_DRIVER_LIB "/boot/driver/fvm.so"
#define STRLEN(s) sizeof(s) / sizeof((s)[0])

// FVM slice size used for tests
constexpr size_t kTestFvmSliceSize = 16 * (1 << 10); // 16kb
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
constexpr uint8_t kTestPartGUID[] = GUID_DATA_VALUE;
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
    if (state_ == FsTestState::kError || state_ == FsTestState::kRunning) {
        EXPECT_EQ(Teardown(true /* minimal */), 0);
    }
}

bool BlobfsTest::Init() {
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
        ASSERT_EQ(wait_for_driver_bind(ramdisk_path_, "fvm"), 0,
                  "[FAILED]: FVM driver never appeared");
        fd.reset();

        // Open "fvm" driver.
        strcpy(fvm_path_, ramdisk_path_);
        strcat(fvm_path_, "/fvm");
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

    ASSERT_EQ(mkfs(ramdisk_path_, DISK_FORMAT_BLOBFS, launch_stdio_sync, &default_mkfs_options),
              ZX_OK, "Could not mkfs blobfs");
    zx_status_t status;
    if ((status = mkfs(ramdisk_path_, DISK_FORMAT_BLOBFS, launch_stdio_sync,
                       &default_mkfs_options)) != ZX_OK) {
        fprintf(stderr, "Could not mkfs blobfs: %d", status);
        destroy_ramdisk(ramdisk_path_);
        ASSERT_TRUE(false);
    }

    ASSERT_TRUE(MountInternal());
    error.cancel();
    state_ = FsTestState::kRunning;
    END_HELPER;
}

bool BlobfsTest::Remount() {
    BEGIN_HELPER;
    ASSERT_EQ(state_, FsTestState::kRunning);
    auto error = fbl::MakeAutoCall([this](){ state_ = FsTestState::kError; });
    ASSERT_EQ(umount(MOUNT_PATH), ZX_OK, "Failed to unmount blobfs");
    ASSERT_TRUE(MountInternal(), "Failed to mount blobfs");
    error.cancel();
    END_HELPER;
}

bool BlobfsTest::Teardown(bool minimal) {
    BEGIN_HELPER;
    ASSERT_NE(state_, FsTestState::kComplete);
    auto error = fbl::MakeAutoCall([this](){ state_ = FsTestState::kError; });

    if (!minimal) {
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
    int fd = open(mount_path, O_RDONLY | O_DIRECTORY);
    ASSERT_GT(fd, 0);
    char buf[sizeof(vfs_query_info_t) + MAX_FS_NAME_LEN + 1];
    vfs_query_info_t* info = reinterpret_cast<vfs_query_info_t*>(buf);
    ssize_t r = ioctl_vfs_query_fs(fd, info, sizeof(buf) - 1);
    ASSERT_EQ(r, (ssize_t)(sizeof(vfs_query_info_t) + strlen("blobfs")), "Failed to query filesystem");
    buf[r] = '\0';
    const char* name = reinterpret_cast<const char*>(buf + sizeof(vfs_query_info_t));
    ASSERT_EQ(strncmp("blobfs", name, strlen("blobfs")), 0, "Unexpected filesystem mounted");
    ASSERT_LE(info->used_nodes, info->total_nodes, "Used nodes greater than free nodes");
    ASSERT_LE(info->used_bytes, info->total_bytes, "Used bytes greater than free bytes");
    ASSERT_EQ(close(fd), 0);
    return true;
}

bool BlobfsTest::MountInternal() {
    BEGIN_HELPER;
    int flags = read_only_ ? O_RDONLY : O_RDWR;

    fbl::unique_fd fd(open(ramdisk_path_, flags));
    ASSERT_TRUE(fd, "Could not open ramdisk");

    mount_options_t options;
    memcpy(&options, &default_mount_options, sizeof(options));

    if (read_only_) {
        options.readonly = true;
    }

    // fd consumed by mount. By default, mount waits until the filesystem is
    // ready to accept commands.
    ASSERT_EQ(mount(fd.get(), MOUNT_PATH, DISK_FORMAT_BLOBFS, &options, launch_stdio_async), ZX_OK,
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
static bool MakeBlob(blob_info_t* info, int* out_fd) {
    int fd = open(info->path, O_CREAT | O_RDWR);
    ASSERT_GT(fd, 0, "Failed to create blob");
    ASSERT_EQ(ftruncate(fd, info->size_data), 0);
    ASSERT_EQ(StreamAll(write, fd, info->data.get(), info->size_data), 0, "Failed to write Data");

    *out_fd = fd;
    ASSERT_TRUE(VerifyContents(*out_fd, info->data.get(), info->size_data));
    return true;
}

static bool MakeBlobUnverified(blob_info_t* info, int* out_fd) {
    int fd = open(info->path, O_CREAT | O_RDWR);
    ASSERT_GT(fd, 0, "Failed to create blob");
    ASSERT_EQ(ftruncate(fd, info->size_data), 0);
    ASSERT_EQ(StreamAll(write, fd, info->data.get(), info->size_data), 0, "Failed to write Data");
    *out_fd = fd;
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
    int fd = open(info->path, O_CREAT | O_RDWR);
    ASSERT_GT(fd, 0, "Failed to create blob");
    ASSERT_EQ(ftruncate(fd, info->size_data), 0);

    // If we're writing a blob with invalid sizes, it's possible that writing will fail.
    StreamAll(write, fd, info->data.get(), info->size_data);

    ASSERT_TRUE(VerifyCompromised(fd, info->data.get(), info->size_data));
    ASSERT_EQ(close(fd), 0);
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

// Creates, writes, reads (to verify) and operates on a blob.
// Returns the result of the post-processing 'func' (true == success).
static bool GenerateBlob(size_t size_data, fbl::unique_ptr<blob_info_t>* out) {
    // Generate a Blob of random data
    fbl::AllocChecker ac;
    fbl::unique_ptr<blob_info_t> info(new (&ac) blob_info_t);
    EXPECT_EQ(ac.check(), true);
    info->data.reset(new (&ac) char[size_data]);
    EXPECT_EQ(ac.check(), true);
    static unsigned int seed = static_cast<unsigned int>(zx_ticks_get());

    for (size_t i = 0; i < size_data; i++) {
        info->data[i] = (char)rand_r(&seed);
    }
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
    return true;
}

bool QueryInfo(size_t expected_nodes, size_t expected_bytes) {
    int fd = open(MOUNT_PATH, O_RDONLY | O_DIRECTORY);
    ASSERT_GT(fd, 0);

    char buf[sizeof(vfs_query_info_t) + MAX_FS_NAME_LEN + 1];
    vfs_query_info_t* info = reinterpret_cast<vfs_query_info_t*>(buf);
    ssize_t rv = ioctl_vfs_query_fs(fd, info, sizeof(buf) - 1);
    ASSERT_EQ(close(fd), 0);

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
        ASSERT_TRUE(GenerateBlob(1 << i, &info));

        int fd;
        ASSERT_TRUE(MakeBlob(info.get(), &fd));
        ASSERT_EQ(close(fd), 0);

        // We can re-open and verify the Blob as read-only
        fd = open(info->path, O_RDONLY);
        ASSERT_GT(fd, 0, "Failed to-reopen blob");
        ASSERT_TRUE(VerifyContents(fd, info->data.get(), info->size_data));
        ASSERT_EQ(close(fd), 0);

        // We cannot re-open the blob as writable
        fd = open(info->path, O_RDWR | O_CREAT);
        ASSERT_LT(fd, 0, "Shouldn't be able to re-create blob that exists");
        fd = open(info->path, O_RDWR);
        ASSERT_LT(fd, 0, "Shouldn't be able to re-open blob as writable");
        fd = open(info->path, O_WRONLY);
        ASSERT_LT(fd, 0, "Shouldn't be able to re-open blob as writable");

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
    ASSERT_TRUE(GenerateBlob(0, &info));

    int fd;
    fd = open(info->path, O_CREAT | O_EXCL | O_RDWR);
    ASSERT_GT(fd, 0);
    ASSERT_EQ(ftruncate(fd, 0), 0);
    char buf[1];
    ASSERT_EQ(read(fd, &buf[0], 1), 0, "Null Blob should reach EOF immediately");
    ASSERT_EQ(close(fd), 0);

    fd = open(info->path, O_CREAT | O_EXCL | O_RDWR);
    ASSERT_LT(fd, 0, "Null Blob should already exist");
    fd = open(info->path, O_CREAT | O_RDWR);
    ASSERT_LT(fd, 0, "Null Blob should not be openable as writable");

    fd = open(info->path, O_RDONLY);
    ASSERT_GT(fd, 0);
    ASSERT_EQ(close(fd), 0);
    ASSERT_EQ(unlink(info->path), 0, "Null Blob should be unlinkable");

    ASSERT_TRUE(blobfsTest.Teardown(), "unmounting blobfs");
    END_TEST;
}

template <FsTestType TestType>
static bool TestMmap(void) {
    BEGIN_TEST;
    BlobfsTest blobfsTest(TestType);
    ASSERT_TRUE(blobfsTest.Init(), "Mounting Blobfs");

    for (size_t i = 10; i < 16; i++) {
        fbl::unique_ptr<blob_info_t> info;
        ASSERT_TRUE(GenerateBlob(1 << i, &info));

        int fd;
        ASSERT_TRUE(MakeBlob(info.get(), &fd));
        ASSERT_EQ(close(fd), 0);
        fd = open(info->path, O_RDONLY);
        ASSERT_GT(fd, 0, "Failed to-reopen blob");

        void* addr = mmap(NULL, info->size_data, PROT_READ, MAP_PRIVATE,
                          fd, 0);
        ASSERT_NE(addr, MAP_FAILED, "Could not mmap blob");
        ASSERT_EQ(memcmp(addr, info->data.get(), info->size_data), 0, "Mmap data invalid");
        ASSERT_EQ(munmap(addr, info->size_data), 0, "Could not unmap blob");
        ASSERT_EQ(close(fd), 0);
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
        ASSERT_TRUE(GenerateBlob(1 << i, &info));

        int fd;
        ASSERT_TRUE(MakeBlob(info.get(), &fd));
        ASSERT_EQ(close(fd), 0);
        fd = open(info->path, O_RDONLY);
        ASSERT_GT(fd, 0, "Failed to-reopen blob");

        void* addr = mmap(NULL, info->size_data, PROT_READ, MAP_PRIVATE, fd, 0);
        ASSERT_NE(addr, MAP_FAILED, "Could not mmap blob");
        ASSERT_EQ(close(fd), 0);

        // We should be able to access the mapped data while the file is closed.
        ASSERT_EQ(memcmp(addr, info->data.get(), info->size_data), 0, "Mmap data invalid");

        // We should be able to re-open and remap the file.
        //
        // Although this isn't being tested explicitly (we lack a mechanism to
        // check that the second mapping uses the same underlying pages as the
        // first) the memory usage should avoid duplication in the second
        // mapping.
        fd = open(info->path, O_RDONLY);
        void* addr2 = mmap(NULL, info->size_data, PROT_READ, MAP_PRIVATE, fd, 0);
        ASSERT_NE(addr2, MAP_FAILED, "Could not mmap blob");
        ASSERT_EQ(close(fd), 0);
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
        ASSERT_TRUE(GenerateBlob(kBlobSize, &info[i]));
        int fd;
        ASSERT_TRUE(MakeBlob(info[i].get(), &fd));
        ASSERT_EQ(close(fd), 0);
        fd = open(info[i]->path, O_RDONLY);
        ASSERT_GT(fd, 0, "Failed to-reopen blob");
        ASSERT_TRUE(VerifyContents(fd, info[i]->data.get(), info[i]->size_data));
        ASSERT_EQ(close(fd), 0);
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
static bool TestQueryInfo(void) {
    BEGIN_TEST;
    ASSERT_EQ(TestType, FsTestType::kFvm);

    BlobfsTest blobfsTest(TestType);
    ASSERT_TRUE(blobfsTest.Init(), "Mounting Blobfs");

    size_t total_bytes = 0;
    ASSERT_TRUE(QueryInfo(0, 0));
    for (size_t i = 10; i < 16; i++) {
        fbl::unique_ptr<blob_info_t> info;
        ASSERT_TRUE(GenerateBlob(1 << i, &info));

        int fd;
        ASSERT_TRUE(MakeBlob(info.get(), &fd));
        ASSERT_EQ(close(fd), 0);
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
        ASSERT_TRUE(GenerateBlob(1 << i, &info));

        int fd;
        ASSERT_TRUE(MakeBlob(info.get(), &fd));

        // We should be able to unlink the blob
        ASSERT_EQ(unlink(info->path), 0, "Failed to unlink");

        // We should still be able to read the blob after unlinking
        ASSERT_TRUE(VerifyContents(fd, info->data.get(), info->size_data));

        // After closing the fd, however, we should not be able to re-open the blob
        ASSERT_EQ(close(fd), 0);
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
        ASSERT_TRUE(GenerateBlob(1 << i, &info));

        int fd;
        ASSERT_TRUE(MakeBlob(info.get(), &fd));

        // After blob generation, writes should be rejected
        ASSERT_LT(write(fd, info->data.get(), 1), 0,
                  "After being written, the blob should refuse writes");

        off_t seek_pos = (rand() % info->size_data);
        ASSERT_EQ(lseek(fd, seek_pos, SEEK_SET), seek_pos);
        ASSERT_LT(write(fd, info->data.get(), 1), 0,
                  "After being written, the blob should refuse writes");
        ASSERT_LT(ftruncate(fd, rand() % info->size_data), 0,
                  "The blob should always refuse to be truncated");

        // We should be able to unlink the blob
        ASSERT_EQ(close(fd), 0);
        ASSERT_EQ(unlink(info->path), 0, "Failed to unlink");
    }

    ASSERT_TRUE(blobfsTest.Teardown(), "unmounting blobfs");
    END_TEST;
}

template <FsTestType TestType>
static bool ReadTooLarge(void) {
    BEGIN_TEST;
    BlobfsTest blobfsTest(TestType);
    ASSERT_TRUE(blobfsTest.Init(), "Mounting Blobfs");

    for (size_t i = 0; i < 16; i++) {
        fbl::unique_ptr<blob_info_t> info;
        ASSERT_TRUE(GenerateBlob(1 << i, &info));

        int fd;
        ASSERT_TRUE(MakeBlob(info.get(), &fd));

        // Verify the contents of the Blob
        fbl::AllocChecker ac;
        fbl::unique_ptr<char[]> buf(new (&ac) char[info->size_data]);
        EXPECT_EQ(ac.check(), true);

        // Try read beyond end of blob
        off_t end_off = info->size_data;
        ASSERT_EQ(lseek(fd, end_off, SEEK_SET), end_off);
        ASSERT_EQ(read(fd, &buf[0], 1), 0, "Expected empty read beyond end of file");

        // Try some reads which straddle the end of the blob
        for (ssize_t j = 1; j < static_cast<ssize_t>(info->size_data); j *= 2) {
            end_off = info->size_data - j;
            ASSERT_EQ(lseek(fd, end_off, SEEK_SET), end_off);
            ASSERT_EQ(read(fd, &buf[0], j * 2), j, "Expected to only read one byte at end of file");
            ASSERT_EQ(memcmp(buf.get(), &info->data[info->size_data - j], j),
                      0, "Read data, but it was bad");
        }

        // We should be able to unlink the blob
        ASSERT_EQ(close(fd), 0);
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
    ASSERT_TRUE(GenerateBlob(1 << 15, &info));

    int fd = open(info->path, O_CREAT | O_RDWR);
    ASSERT_GT(fd, 0, "Failed to create blob");
    ASSERT_EQ(ftruncate(fd, 0), -1, "Blob without data doesn't match null blob");
    // This is the size of the entire disk; we won't have room.
    ASSERT_EQ(ftruncate(fd, blobfsTest.GetDiskSize()), -1, "Huge blob");

    // Okay, finally, a valid blob!
    ASSERT_EQ(ftruncate(fd, info->size_data), 0, "Failed to allocate blob");

    // Write nothing, but close the blob. Since the write was incomplete,
    // it will be inaccessible.
    ASSERT_EQ(close(fd), 0);
    ASSERT_LT(open(info->path, O_RDWR), 0, "Cannot access partial blob");
    ASSERT_LT(open(info->path, O_RDONLY), 0, "Cannot access partial blob");

    // And once more -- let's write everything but the last byte of a blob's data.
    fd = open(info->path, O_CREAT | O_RDWR);
    ASSERT_GT(fd, 0, "Failed to create blob");
    ASSERT_EQ(ftruncate(fd, info->size_data), 0, "Failed to allocate blob");
    ASSERT_EQ(StreamAll(write, fd, info->data.get(), info->size_data - 1), 0,
              "Failed to write data");
    ASSERT_EQ(close(fd), 0);
    ASSERT_LT(open(info->path, O_RDWR), 0, "Cannot access partial blob");

    ASSERT_TRUE(blobfsTest.Teardown(), "unmounting blobfs");
    END_TEST;
}

template <FsTestType TestType>
static bool CorruptedBlob(void) {
    BEGIN_TEST;
    BlobfsTest blobfsTest(TestType);
    ASSERT_TRUE(blobfsTest.Init(), "Mounting Blobfs");

    fbl::unique_ptr<blob_info_t> info;
    for (size_t i = 1; i < 18; i++) {
        ASSERT_TRUE(GenerateBlob(1 << i, &info));
        info->size_data -= (rand() % info->size_data) + 1;
        if (info->size_data == 0) {
            info->size_data = 1;
        }
        ASSERT_TRUE(MakeBlobCompromised(info.get()));
    }

    for (size_t i = 0; i < 18; i++) {
        ASSERT_TRUE(GenerateBlob(1 << i, &info));
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
    ASSERT_TRUE(blobfsTest.Init(), "Mounting Blobfs");

    fbl::unique_ptr<blob_info_t> info;
    for (size_t i = 1; i < 18; i++) {
        ASSERT_TRUE(GenerateBlob(1 << i, &info));

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
        ASSERT_TRUE(GenerateBlob(1 << i, &info));
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
            ASSERT_TRUE(GenerateBlob((1 << i) + j, &info));
            int fd;
            ASSERT_TRUE(MakeBlob(info.get(), &fd));
            ASSERT_EQ(unlink(info->path), 0);
            ASSERT_EQ(close(fd), 0);
        }
    }
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
        ASSERT_TRUE(GenerateBlob(1 << i, &info));

        int fd;
        ASSERT_TRUE(MakeBlob(info.get(), &fd));
        // Close fd, unmount filesystem
        ASSERT_EQ(close(fd), 0);

        ASSERT_TRUE(blobfsTest.Remount(), "Could not re-mount blobfs");

        fd = open(info->path, O_RDONLY);
        ASSERT_GT(fd, 0, "Failed to open blob");

        ASSERT_TRUE(VerifyContents(fd, info->data.get(), info->size_data));
        ASSERT_EQ(close(fd), 0, "Could not close blob");
        ASSERT_EQ(unlink(info->path), 0);
    }

    ASSERT_TRUE(blobfsTest.Teardown(), "unmounting blobfs");
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
    int fd;
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
    ASSERT_TRUE(GenerateBlob(1 + (rand_r(seed) % (1 << 16)), &info));

    fbl::AllocChecker ac;
    fbl::unique_ptr<blob_state_t> state(new (&ac) blob_state(fbl::move(info)));
    ASSERT_EQ(ac.check(), true);

    {
        fbl::AutoLock al(&bl->list_lock);

        if (bl->blob_count >= max_blobs) {
            return true;
        }
        int fd = open(state->info->path, O_CREAT | O_RDWR);
        ASSERT_GT(fd, 0, "Failed to create blob");
        state->fd = fd;

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
        ASSERT_EQ(ftruncate(state->fd, state->info->size_data), 0);
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
        ASSERT_EQ(StreamAll(write, state->fd, state->info->data.get() + bytes_offset, bytes_write),
                  0, "Failed to write Data");

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
        ASSERT_TRUE(VerifyContents(state->fd, state->info->data.get(),
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
    ASSERT_EQ(close(state->fd), 0, "Could not close blob");
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
        ASSERT_EQ(close(state->fd), 0, "Could not close blob");
        int fd = open(state->info->path, O_RDONLY);
        ASSERT_GT(fd, 0, "Failed to reopen blob");
        state->fd = fd;
    }
    {
        fbl::AutoLock al(&bl->list_lock);
        bl->list.push_front(fbl::move(state));
    }
    return true;
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
        ASSERT_EQ(close(state.fd), 0);
    }

    // Unmount, remount
    ASSERT_TRUE(blobfsTest.Remount(), "Could not re-mount blobfs");

    for (auto& state : bl.list) {
        if (state.state == readable) {
            // If a blob was readable before being unmounted, it should still exist.
            int fd = open(state.info->path, O_RDONLY);
            ASSERT_GT(fd, 0, "Failed to create blob");
            ASSERT_TRUE(VerifyContents(fd, state.info->data.get(),
                                       state.info->size_data));
            ASSERT_EQ(unlink(state.info->path), 0);
            ASSERT_EQ(close(fd), 0);
        } else {
            // ... otherwise, the blob should have been deleted.
            ASSERT_LT(open(state.info->path, O_RDONLY), 0);
        }
    }

    ASSERT_TRUE(blobfsTest.Teardown(), "unmounting blobfs");
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
        int fd = open(dat->path, O_RDONLY);
        ASSERT_GT(fd, 0);
        ASSERT_EQ(close(fd), 0);
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
    ASSERT_TRUE(GenerateBlob(1 << 10, &anchor_info));

    fbl::unique_ptr<blob_info_t> info;
    ASSERT_TRUE(GenerateBlob(10 *(1 << 20), &info));
    reopen_data_t dat;
    strcpy(dat.path, info->path);

    for (size_t i = 0; i < num_ops; i++) {
        printf("Running op %lu... ", i);
        int fd;
        int anchor_fd;
        atomic_store(&dat.complete, false);

        // Write both blobs to disk (without verification, so we can start reopening the blob asap)
        ASSERT_TRUE(MakeBlobUnverified(info.get(), &fd));
        ASSERT_TRUE(MakeBlobUnverified(anchor_info.get(), &anchor_fd));
        ASSERT_EQ(close(fd), 0);

        thrd_t thread;
        ASSERT_EQ(thrd_create(&thread, reopen_thread, &dat), thrd_success);

        // Sleep while the thread continually opens and closes the blob
        usleep(1000000);
        ASSERT_EQ(syncfs(anchor_fd), 0);
        atomic_store(&dat.complete, true);

        int res;
        ASSERT_EQ(thrd_join(thread, &res), thrd_success);
        ASSERT_EQ(res, 0);

        ASSERT_EQ(close(anchor_fd), 0);
        ASSERT_EQ(unlink(info->path), 0);
        ASSERT_EQ(unlink(anchor_info->path), 0);
    }

    ASSERT_TRUE(blobfsTest.Teardown(), "unmounting Blobfs");
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
        ASSERT_EQ(close(state.fd), 0);
    }

    // Unmount, remount
    ASSERT_TRUE(blobfsTest.Remount(), "Could not re-mount blobfs");

    for (auto& state : bl.list) {
        if (state.state == readable) {
            // If a blob was readable before being unmounted, it should still exist.
            int fd = open(state.info->path, O_RDONLY);
            ASSERT_GT(fd, 0, "Failed to create blob");
            ASSERT_TRUE(VerifyContents(fd, state.info->data.get(),
                                       state.info->size_data));
            ASSERT_EQ(unlink(state.info->path), 0);
            ASSERT_EQ(close(fd), 0);
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
        ASSERT_TRUE(GenerateBlob(1 << 17, &info));

        int fd = open(info->path, O_CREAT | O_RDWR);
        ASSERT_GT(fd, 0, "Failed to create blob");
        int r = ftruncate(fd, info->size_data);
        if (r < 0) {
            ASSERT_EQ(errno, ENOSPC, "Blobfs expected to run out of space");
            // We ran out of space, as expected. Can we allocate if we
            // unlink a previously allocated blob of the desired size?
            ASSERT_EQ(unlink(last_info->path), 0, "Unlinking old blob");
            ASSERT_EQ(ftruncate(fd, info->size_data), 0, "Re-init after unlink");

            // Yay! allocated successfully.
            ASSERT_EQ(close(fd), 0);
            break;
        }
        ASSERT_EQ(StreamAll(write, fd, info->data.get(), info->size_data), 0,
                  "Failed to write Data");
        ASSERT_EQ(close(fd), 0);
        last_info = fbl::move(info);

        if (++count % 50 == 0) {
            printf("Allocated %lu blobs\n", count);
        }
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

    ASSERT_TRUE(GenerateBlob(1 << 17, &info));
    int fd = open(info->path, O_CREAT | O_EXCL | O_RDWR);
    ASSERT_GT(fd, 0, "Failed to create blob");

    ASSERT_LT(open(info->path, O_CREAT | O_EXCL | O_RDWR), 0,
              "Should not be able to exclusively create twice");

    // This second fd should also not be readable
    int fd2 = open(info->path, O_CREAT | O_RDWR);
    ASSERT_GT(fd2, 0, "Failed to create blob");

    ASSERT_TRUE(check_not_readable(fd), "Should not be readable after open");
    ASSERT_TRUE(check_not_readable(fd2), "Should not be readable after open");
    ASSERT_EQ(ftruncate(fd, info->size_data), 0);
    ASSERT_TRUE(check_not_readable(fd), "Should not be readable after alloc");
    ASSERT_TRUE(check_not_readable(fd2), "Should not be readable after alloc");
    ASSERT_EQ(StreamAll(write, fd, info->data.get(), info->size_data), 0,
              "Failed to write Data");

    // Okay, NOW we can read.
    // Double check that attempting to read early didn't cause problems...
    ASSERT_TRUE(VerifyContents(fd, info->data.get(), info->size_data));
    ASSERT_TRUE(VerifyContents(fd2, info->data.get(), info->size_data));

    // Cool, everything is readable. What if we try accessing the blob status now?
    EXPECT_TRUE(check_readable(fd));

    ASSERT_EQ(close(fd), 0);
    ASSERT_EQ(close(fd2), 0);
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

    ASSERT_TRUE(GenerateBlob(1 << 17, &info));
    int fd = open(info->path, O_CREAT | O_EXCL | O_RDWR);
    ASSERT_GT(fd, 0, "Failed to create blob");

    ASSERT_LT(open(info->path, O_CREAT | O_EXCL | O_RDWR), 0,
              "Should not be able to exclusively create twice");

    // Launch a background thread to wait for fd to become readable
    auto wait_until_readable = [](void* arg) {
        int fd = *static_cast<int*>(arg);
        EXPECT_TRUE(wait_readable(fd));
        EXPECT_TRUE(check_readable(fd));
        EXPECT_EQ(close(fd), 0);
        return 0;
    };
    int dupfd = dup(fd);
    ASSERT_GT(dupfd, 0, "Could not dup fd");
    thrd_t waiter_thread;
    thrd_create(&waiter_thread, wait_until_readable, &dupfd);

    ASSERT_TRUE(check_not_readable(fd), "Should not be readable after open");
    ASSERT_EQ(ftruncate(fd, info->size_data), 0);
    ASSERT_TRUE(check_not_readable(fd), "Should not be readable after alloc");
    ASSERT_EQ(StreamAll(write, fd, info->data.get(), info->size_data), 0,
              "Failed to write Data");

    // Cool, everything is readable. What if we try accessing the blob status now?
    EXPECT_TRUE(check_readable(fd));

    // Our background thread should have also completed successfully...
    int result;
    ASSERT_EQ(thrd_join(waiter_thread, &result), 0, "thrd_join failed");
    ASSERT_EQ(result, 0, "Unexpected result from background thread");

    // Double check that attempting to read early didn't cause problems...
    ASSERT_TRUE(VerifyContents(fd, info->data.get(), info->size_data));
    ASSERT_EQ(close(fd), 0);
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
    ASSERT_TRUE(GenerateBlob(1 << 17, &info));
    int fd = open(info->path, O_CREAT | O_RDWR);
    ASSERT_GT(fd, 0, "Failed to create blob");
    ASSERT_EQ(ftruncate(fd, info->size_data), 0);

    size_t n = 0;
    while (n != info->size_data) {
        off_t seek_pos = (rand() % info->size_data);
        ASSERT_EQ(lseek(fd, seek_pos, SEEK_SET), seek_pos);
        ssize_t d = write(fd, info->data.get(), info->size_data - n);
        ASSERT_GT(d, 0, "Data Write error");
        n += d;
    }

    // Double check that attempting to seek early didn't cause problems...
    ASSERT_TRUE(VerifyContents(fd, info->data.get(), info->size_data));
    ASSERT_EQ(close(fd), 0);
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
    auto full_unlink_reopen = [](int& fd, const char* path) {
        ASSERT_EQ(unlink(path), 0);
        ASSERT_EQ(close(fd), 0);
        fd = open(path, O_CREAT | O_RDWR | O_EXCL);
        ASSERT_GT(fd, 0, "Failed to recreate blob");
        return true;
    };

    fbl::unique_ptr<blob_info_t> info;
    ASSERT_TRUE(GenerateBlob(1 << 17, &info));

    int fd = open(info->path, O_CREAT | O_RDWR);
    ASSERT_GT(fd, 0, "Failed to create blob");

    // Unlink after first open
    ASSERT_TRUE(full_unlink_reopen(fd, info->path));

    // Unlink after init
    ASSERT_EQ(ftruncate(fd, info->size_data), 0);
    ASSERT_TRUE(full_unlink_reopen(fd, info->path));

    // Unlink after first write
    ASSERT_EQ(ftruncate(fd, info->size_data), 0);
    ASSERT_EQ(StreamAll(write, fd, info->data.get(), info->size_data), 0,
              "Failed to write Data");
    ASSERT_TRUE(full_unlink_reopen(fd, info->path));
    ASSERT_EQ(unlink(info->path), 0);
    ASSERT_EQ(close(fd), 0);
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
    ASSERT_TRUE(GenerateBlob(1 << 12, &info));
    int fd;
    ASSERT_TRUE(MakeBlob(info.get(), &fd));
    ASSERT_TRUE(VerifyContents(fd, info->data.get(), info->size_data));

    // Neat. Now, let's try some unsupported operations
    ASSERT_LT(rename(info->path, info->path), 0);
    ASSERT_LT(truncate(info->path, 0), 0);
    ASSERT_LT(utime(info->path, nullptr), 0);

    // Test that a blob fd cannot unmount the entire blobfs.
    ASSERT_LT(ioctl_vfs_unmount_fs(fd), 0);

    // Access the file once more, after these operations
    ASSERT_TRUE(VerifyContents(fd, info->data.get(), info->size_data));
    ASSERT_EQ(unlink(info->path), 0);
    ASSERT_EQ(close(fd), 0);

    ASSERT_TRUE(blobfsTest.Teardown(), "unmounting blobfs");
    END_TEST;
}

template <FsTestType TestType>
static bool RootDirectory(void) {
    // Attempt operations on the root directory
    BEGIN_TEST;
    BlobfsTest blobfsTest(TestType);
    ASSERT_TRUE(blobfsTest.Init(), "Mounting Blobfs");

    int dirfd = open(MOUNT_PATH "/.", O_RDONLY);
    ASSERT_GT(dirfd, 0, "Cannot open root directory");

    fbl::unique_ptr<blob_info_t> info;
    ASSERT_TRUE(GenerateBlob(1 << 12, &info));

    // Test ioctls which should ONLY operate on Blobs
    ASSERT_LT(ftruncate(dirfd, info->size_data), 0);

    char buf[8];
    ASSERT_LT(write(dirfd, buf, 8), 0, "Should not write to directory");
    ASSERT_LT(read(dirfd, buf, 8), 0, "Should not read from directory");

    // Should NOT be able to unlink root dir
    ASSERT_EQ(close(dirfd), 0);
    ASSERT_LT(unlink(info->path), 0);

    ASSERT_TRUE(blobfsTest.Teardown(), "unmounting blobfs");
    END_TEST;
}

template <FsTestType TestType>
static bool QueryDevicePath(void) {
    BEGIN_TEST;
    BlobfsTest blobfsTest(TestType);
    ASSERT_TRUE(blobfsTest.Init(), "Mounting Blobfs");

    int dirfd = open(MOUNT_PATH "/.", O_RDONLY | O_ADMIN);
    ASSERT_GT(dirfd, 0, "Cannot open root directory");

    char device_path[1024];
    ssize_t path_len = ioctl_vfs_get_device_path(dirfd, device_path, sizeof(device_path));
    ASSERT_GT(path_len, 0, "Device path not found");

    char actual_path[PATH_MAX];
    ASSERT_TRUE(blobfsTest.GetDevicePath(actual_path, PATH_MAX));
    ASSERT_EQ(strncmp(actual_path, device_path, path_len), 0, "Unexpected device path");
    ASSERT_EQ(close(dirfd), 0);

    dirfd = open(MOUNT_PATH "/.", O_RDONLY);
    ASSERT_GT(dirfd, 0, "Cannot open root directory");
    path_len = ioctl_vfs_get_device_path(dirfd, device_path, sizeof(device_path));
    ASSERT_LT(path_len, 0);
    ASSERT_EQ(close(dirfd), 0);

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
    ASSERT_TRUE(GenerateBlob(1 << 10, &info));
    int blob_fd;
    ASSERT_TRUE(MakeBlob(info.get(), &blob_fd));
    ASSERT_TRUE(VerifyContents(blob_fd, info->data.get(), info->size_data));
    ASSERT_EQ(close(blob_fd), 0);

    blobfsTest.SetReadOnly(true);
    ASSERT_TRUE(blobfsTest.Remount());

    // We can read old blobs
    blob_fd = open(info->path, O_RDONLY);
    ASSERT_GE(blob_fd, 0);
    ASSERT_TRUE(VerifyContents(blob_fd, info->data.get(), info->size_data));
    ASSERT_EQ(close(blob_fd), 0);

    // We cannot create new blobs
    ASSERT_TRUE(GenerateBlob(1 << 10, &info));
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
        ASSERT_TRUE(GenerateBlob(64, &info));

        int fd;
        ASSERT_TRUE(MakeBlob(info.get(), &fd));
        ASSERT_EQ(close(fd), 0);
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
    ASSERT_GT(fd.get(), 0, "Could not open ramdisk");

    // Manually grow slice so FVM will differ from Blobfs.
    extend_request_t extend_request;
    extend_request.offset = (0x10000 / kBlocksPerSlice) + 1;
    extend_request.length = 1;
    ASSERT_EQ(ioctl_block_fvm_extend(fd.get(), &extend_request), 0);

    // Attempt to mount the VPart.
    ASSERT_NE(mount(fd.release(), MOUNT_PATH, DISK_FORMAT_BLOBFS, &default_mount_options,
                    launch_stdio_async), ZX_OK);

    // Clean up.
    ASSERT_TRUE(blobfsTest.Teardown(true), "unmounting Blobfs");
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
    ASSERT_TRUE(GenerateBlob(size, &info_complete));
    ASSERT_TRUE(GenerateBlob(size, &info_partial));

    // Partially write out first blob.
    int fd_partial = open(info_partial->path, O_CREAT | O_RDWR);
    ASSERT_GT(fd_partial, 0, "Failed to create blob");
    ASSERT_EQ(ftruncate(fd_partial, size), 0);
    ASSERT_EQ(StreamAll(write, fd_partial, info_partial->data.get(), size / 2), 0,
              "Failed to write Data");

    // Completely write out second blob.
    int fd_complete;
    ASSERT_TRUE(MakeBlob(info_complete.get(), &fd_complete));

    ASSERT_EQ(close(fd_complete), 0);
    ASSERT_EQ(close(fd_partial), 0);

    ASSERT_TRUE(blobfsTest.Teardown(), "unmounting blobfs");
    END_TEST;
}

template <FsTestType TestType>
bool TestPartialWriteSleepRamdisk(void) {
    BEGIN_TEST;
    BlobfsTest blobfsTest(TestType);
    ASSERT_TRUE(blobfsTest.Init(), "Mounting Blobfs");
    fbl::unique_ptr<blob_info_t> info_complete;
    fbl::unique_ptr<blob_info_t> info_partial;
    size_t size = 1 << 20;
    ASSERT_TRUE(GenerateBlob(size, &info_complete));
    ASSERT_TRUE(GenerateBlob(size, &info_partial));

    // Partially write out first blob.
    int fd_partial = open(info_partial->path, O_CREAT | O_RDWR);
    ASSERT_GT(fd_partial, 0, "Failed to create blob");
    ASSERT_EQ(ftruncate(fd_partial, size), 0);
    ASSERT_EQ(StreamAll(write, fd_partial, info_partial->data.get(), size / 2), 0,
              "Failed to write Data");

    // Completely write out second blob.
    int fd_complete;
    ASSERT_TRUE(MakeBlob(info_complete.get(), &fd_complete));

    ASSERT_EQ(syncfs(fd_complete), 0);
    ASSERT_TRUE(blobfsTest.ToggleSleep());

    ASSERT_EQ(close(fd_complete), 0);
    ASSERT_EQ(close(fd_partial), 0);

    fd_complete = open(info_complete->path, O_RDONLY);
    ASSERT_GT(fd_complete, 0, "Failed to re-open blob");

    ASSERT_EQ(syncfs(fd_complete), 0);
    ASSERT_TRUE(blobfsTest.ToggleSleep());

    ASSERT_TRUE(VerifyContents(fd_complete, info_complete->data.get(), size));

    fd_partial = open(info_partial->path, O_RDONLY);
    ASSERT_LT(fd_partial, 0, "Should not be able to open invalid blob");
    ASSERT_EQ(close(fd_complete), 0);

    ASSERT_TRUE(blobfsTest.Teardown(), "unmounting Blobfs");
    END_TEST;
}

template <FsTestType TestType>
bool WriteAfterUnlink(void) {
    BEGIN_TEST;
    BlobfsTest blobfsTest(TestType);
    ASSERT_TRUE(blobfsTest.Init(), "Mounting Blobfs");
    fbl::unique_ptr<blob_info_t> info;
    size_t size = 1 << 20;
    ASSERT_TRUE(GenerateBlob(size, &info));

    // Partially write out first blob.
    int fd1 = open(info->path, O_CREAT | O_RDWR);
    ASSERT_GT(fd1, 0, "Failed to create blob");
    ASSERT_EQ(ftruncate(fd1, size), 0);
    ASSERT_EQ(StreamAll(write, fd1, info->data.get(), size / 2), 0, "Failed to write Data");

    ASSERT_EQ(unlink(info->path), 0);
    ASSERT_EQ(StreamAll(write, fd1, info->data.get() + size / 2, size - (size / 2)), 0, "Failed to write Data");
    ASSERT_EQ(close(fd1), 0);
    ASSERT_LT(open(info->path, O_RDONLY), 0);
    ASSERT_TRUE(blobfsTest.Teardown(), "unmounting Blobfs");
    END_TEST;
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
        ASSERT_TRUE(check_readable(state.fd));
    }

    for (size_t i = 0; i < num_blobs; i++) {
        ASSERT_TRUE(blob_read_data_helper(&bl));
    }

    for (auto& state : bl.list) {
        ASSERT_EQ(close(state.fd), 0);
    }
    ASSERT_TRUE(blobfsTest.Teardown(), "Unmounting Blobfs");
    END_TEST;
}

BEGIN_TEST_CASE(blobfs_tests)
RUN_TEST_FOR_ALL_TYPES(MEDIUM, TestBasic)
RUN_TEST_FOR_ALL_TYPES(MEDIUM, TestNullBlob)
RUN_TEST_FOR_ALL_TYPES(MEDIUM, TestMmap)
RUN_TEST_FOR_ALL_TYPES(MEDIUM, TestMmapUseAfterClose)
RUN_TEST_FOR_ALL_TYPES(MEDIUM, TestReaddir)
RUN_TEST_MEDIUM(TestQueryInfo<FsTestType::kFvm>)
RUN_TEST_FOR_ALL_TYPES(MEDIUM, UseAfterUnlink)
RUN_TEST_FOR_ALL_TYPES(MEDIUM, WriteAfterRead)
RUN_TEST_FOR_ALL_TYPES(MEDIUM, WriteAfterUnlink)
RUN_TEST_FOR_ALL_TYPES(MEDIUM, ReadTooLarge)
RUN_TEST_FOR_ALL_TYPES(MEDIUM, BadAllocation)
RUN_TEST_FOR_ALL_TYPES(MEDIUM, CorruptedBlob)
RUN_TEST_FOR_ALL_TYPES(MEDIUM, CorruptedDigest)
RUN_TEST_FOR_ALL_TYPES(MEDIUM, EdgeAllocation)
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
RUN_TEST_FOR_ALL_TYPES(LARGE, CreateUmountRemountLargeMultithreaded)
RUN_TEST_FOR_ALL_TYPES(LARGE, CreateUmountRemountLarge)
RUN_TEST_FOR_ALL_TYPES(LARGE, NoSpace)
RUN_TEST_FOR_ALL_TYPES(MEDIUM, QueryDevicePath)
RUN_TEST_FOR_ALL_TYPES(MEDIUM, TestReadOnly)
RUN_TEST_MEDIUM(ResizePartition<FsTestType::kFvm>)
RUN_TEST_MEDIUM(CorruptAtMount<FsTestType::kFvm>)
RUN_TEST_FOR_ALL_TYPES(LARGE, CreateWriteReopen)
END_TEST_CASE(blobfs_tests)

int main(int argc, char** argv) {
    gUseRealDisk = false;
    int i = 1;
    while (i < argc - 1) {
        if ((strlen(argv[i]) == 2) && (argv[i][0] == '-') && (argv[i][1] == 'd')) {
            if (strnlen(argv[i + 1], PATH_MAX) > 0) {
                fbl::unique_fd fd(open(argv[i + 1], O_RDWR));
                if (!fd) {
                    fprintf(stderr, "[fs] Could not open block device\n");
                    return -1;
                } else if (ioctl_device_get_topo_path(fd.get(), gRealDiskInfo.disk_path, PATH_MAX)
                           < 0) {
                    fprintf(stderr, "[fs] Could not acquire topological path of block device\n");
                    return -1;
                }

                block_info_t block_info;
                ssize_t rc = ioctl_block_get_info(fd.get(), &block_info);

                if (rc < 0 || rc != sizeof(block_info)) {
                    fprintf(stderr, "[fs] Could not query block device info\n");
                    return -1;
                }

                // If we previously tried running tests on this disk, it may
                // have created an FVM and failed. (Try to) clean up from previous state
                // before re-running.
                fvm_destroy(gRealDiskInfo.disk_path);
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
                break;
            }
        }
        i += 1;
    }

    // Destroy all existing partitions that match the test GUIDs
    while (destroy_partition(kTestUniqueGUID, kTestPartGUID) == ZX_OK) {}

    return unittest_run_all_tests(argc, argv) ? 0 : -1;
}
