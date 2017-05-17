// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <poll.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <threads.h>
#include <time.h>
#include <unistd.h>
#include <utime.h>

#include <fs-management/mount.h>
#include <fs-management/ramdisk.h>
#include <magenta/device/ramdisk.h>
#include <magenta/device/vfs.h>
#include <magenta/syscalls.h>
#include <merkle/digest.h>
#include <merkle/tree.h>
#include <mxalloc/new.h>
#include <mxtl/algorithm.h>
#include <mxtl/auto_lock.h>
#include <mxtl/intrusive_double_list.h>
#include <mxtl/unique_ptr.h>
#include <unittest/unittest.h>

#define MOUNT_PATH "/tmp/magenta-blobstore-test"

// Helper functions for mounting Blobstore:

// Unmounts a blobstore and removes the backing ramdisk device.
static int EndBlobstoreTest(const char* ramdisk_path) {
    mx_status_t status = umount(MOUNT_PATH);
    if (status != NO_ERROR) {
        fprintf(stderr, "Failed to unmount filesystem: %d\n", status);
        return -1;
    }
    return destroy_ramdisk(ramdisk_path);
}

static int MountBlobstore(const char* ramdisk_path) {
    int fd = open(ramdisk_path, O_RDWR);
    if (fd < 0) {
        fprintf(stderr, "Could not open ramdisk\n");
        return -1;
    }

    // fd consumed by mount. By default, mount waits until the filesystem is
    // ready to accept commands.
    mx_status_t status;
    if ((status = mount(fd, MOUNT_PATH, DISK_FORMAT_BLOBFS, &default_mount_options,
                        launch_stdio_async)) != NO_ERROR) {
        fprintf(stderr, "Could not mount blobstore: %d\n", status);
        destroy_ramdisk(ramdisk_path);
        return -1;
    }

    return 0;
}

// Creates a ramdisk, formats it, and mounts it at a mount point.
static int StartBlobstoreTest(uint64_t blk_size, uint64_t blk_count, char* ramdisk_path_out) {
    int dirfd = mkdir(MOUNT_PATH, 0755);
    if ((dirfd < 0) && errno != EEXIST) {
        fprintf(stderr, "Could not create mount point for test filesystems\n");
        return -1;
    } else if (dirfd > 0) {
        close(dirfd);
    }

    if (create_ramdisk("blobstore", ramdisk_path_out, blk_size, blk_count)) {
        fprintf(stderr, "Blobstore: Could not create ramdisk\n");
        return -1;
    }

    mx_status_t status;
    if ((status = mkfs(ramdisk_path_out, DISK_FORMAT_BLOBFS, launch_stdio_sync,
                       &default_mkfs_options)) != NO_ERROR) {
        fprintf(stderr, "Could not mkfs blobstore: %d", status);
        destroy_ramdisk(ramdisk_path_out);
        return -1;
    }

    return MountBlobstore(ramdisk_path_out);
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
    AllocChecker ac;
    mxtl::unique_ptr<char[]> buf(new (&ac) char[size_data]);
    EXPECT_EQ(ac.check(), true, "");

    ASSERT_EQ(lseek(fd, 0, SEEK_SET), 0, "");
    ASSERT_EQ(StreamAll(read, fd, &buf[0], size_data), 0, "Failed to read data");
    ASSERT_EQ(memcmp(buf.get(), data, size_data), 0, "Read data, but it was bad");
    return true;
}

// Creates an open blob with the provided Merkle tree + Data, and
// reads to verify the data.
static bool MakeBlob(const char* path, const char* merkle, size_t size_merkle,
                     const char* data, size_t size_data, int* out_fd) {
    int fd = open(path, O_CREAT | O_RDWR);
    ASSERT_GT(fd, 0, "Failed to create blob");
    ASSERT_EQ(ftruncate(fd, size_data), 0, "");
    ASSERT_EQ(StreamAll(write, fd, data, size_data), 0, "Failed to write Data");

    *out_fd = fd;
    ASSERT_TRUE(VerifyContents(*out_fd, data, size_data), "");
    return true;
}

static bool VerifyCompromised(int fd, const char* data, size_t size_data) {
    // Verify the contents of the Blob
    AllocChecker ac;
    mxtl::unique_ptr<char[]> buf(new (&ac) char[size_data]);
    EXPECT_EQ(ac.check(), true, "");

    ASSERT_EQ(lseek(fd, 0, SEEK_SET), 0, "");
    ASSERT_EQ(StreamAll(read, fd, &buf[0], size_data), -1, "Expected reading to fail");
    return true;
}

// Creates an open blob with the provided Merkle tree + Data, and
// reads to verify the data.
static bool MakeBlobCompromised(const char* path, const char* merkle, size_t size_merkle,
                                const char* data, size_t size_data) {
    int fd = open(path, O_CREAT | O_RDWR);
    ASSERT_GT(fd, 0, "Failed to create blob");
    ASSERT_EQ(ftruncate(fd, size_data), 0, "");

    // If we're writing a blob with invalid sizes, it's possible that writing will fail.
    StreamAll(write, fd, data, size_data);

    ASSERT_TRUE(VerifyCompromised(fd, data, size_data), "");
    ASSERT_EQ(close(fd), 0, "");
    return true;
}

static bool uint8_to_hex_str(const uint8_t* data, char* hex_str) {
    for (size_t i = 0; i < 32; i++) {
        ASSERT_EQ(sprintf(hex_str + (i*2), "%02x", data[i]), 2,
                  "Error converting name to string");
    }
    hex_str[64] = 0;
    return true;
}

// An in-memory representation of a blob.
typedef struct blob_info {
    char path[PATH_MAX];
    mxtl::unique_ptr<char[]> merkle;
    size_t size_merkle;
    mxtl::unique_ptr<char[]> data;
    size_t size_data;
} blob_info_t;

// Creates, writes, reads (to verify) and operates on a blob.
// Returns the result of the post-processing 'func' (true == success).
static bool GenerateBlob(size_t size_data, mxtl::unique_ptr<blob_info_t>* out) {
    // Generate a Blob of random data
    AllocChecker ac;
    mxtl::unique_ptr<blob_info_t> info(new (&ac) blob_info_t);
    EXPECT_EQ(ac.check(), true, "");
    info->data.reset(new (&ac) char[size_data]);
    EXPECT_EQ(ac.check(), true, "");
    unsigned int seed = static_cast<unsigned int>(mx_ticks_get());
    for (size_t i = 0; i < size_data; i++) {
        info->data[i] = (char) rand_r(&seed);
    }
    info->size_data = size_data;

    // Generate the Merkle Tree
    merkle::Tree tree;
    info->size_merkle = tree.GetTreeLength(size_data);
    if (info->size_merkle == 0) {
        info->merkle = nullptr;
    } else {
        info->merkle.reset(new (&ac) char[info->size_merkle]);
        ASSERT_EQ(ac.check(), true, "");
    }
    merkle::Digest digest;
    ASSERT_EQ(tree.Create(&info->data[0], info->size_data, &info->merkle[0],
                          info->size_merkle, &digest), NO_ERROR,
              "Couldn't create Merkle Tree");
    strcpy(info->path, MOUNT_PATH "/");
    size_t prefix_len = strlen(info->path);
    digest.ToString(info->path + prefix_len, sizeof(info->path) - prefix_len);

    // Sanity-check the merkle tree
    ASSERT_EQ(tree.Verify(&info->data[0], info->size_data, &info->merkle[0],
                          info->size_merkle, 0, info->size_data, digest),
              NO_ERROR, "Failed to validate Merkle Tree");

    *out = mxtl::move(info);
    return true;
}

// Actual tests:

static bool TestBasic(void) {
    BEGIN_TEST;
    char ramdisk_path[PATH_MAX];
    ASSERT_EQ(StartBlobstoreTest(512, 1 << 20, ramdisk_path), 0, "Mounting Blobstore");

    for (size_t i = 10; i < 16; i++) {
        mxtl::unique_ptr<blob_info_t> info;
        ASSERT_TRUE(GenerateBlob(1 << i, &info), "");

        int fd;
        ASSERT_TRUE(MakeBlob(info->path, info->merkle.get(), info->size_merkle,
                             info->data.get(), info->size_data, &fd), "");
        ASSERT_EQ(close(fd), 0, "");
        fd = open(info->path, O_RDWR);
        ASSERT_GT(fd, 0, "Failed to-reopen blob");

        ASSERT_TRUE(VerifyContents(fd, info->data.get(), info->size_data), "");

        ASSERT_EQ(close(fd), 0, "");
        ASSERT_EQ(unlink(info->path), 0, "");
    }

    ASSERT_EQ(EndBlobstoreTest(ramdisk_path), 0, "unmounting blobstore");
    END_TEST;
}

static bool TestMmap(void) {
    BEGIN_TEST;
    char ramdisk_path[PATH_MAX];
    ASSERT_EQ(StartBlobstoreTest(512, 1 << 20, ramdisk_path), 0, "Mounting Blobstore");

    for (size_t i = 10; i < 16; i++) {
        mxtl::unique_ptr<blob_info_t> info;
        ASSERT_TRUE(GenerateBlob(1 << i, &info), "");

        int fd;
        ASSERT_TRUE(MakeBlob(info->path, info->merkle.get(), info->size_merkle,
                             info->data.get(), info->size_data, &fd), "");
        ASSERT_EQ(close(fd), 0, "");
        fd = open(info->path, O_RDWR);
        ASSERT_GT(fd, 0, "Failed to-reopen blob");

        void* addr = mmap(NULL, info->size_data, PROT_READ, MAP_SHARED,
                          fd, 0);
        ASSERT_NEQ(addr, MAP_FAILED, "Could not mmap blob");
        ASSERT_EQ(memcmp(addr, info->data.get(), info->size_data), 0, "Mmap data invalid");
        ASSERT_EQ(munmap(addr, info->size_data), 0, "Could not unmap blob");
        ASSERT_EQ(close(fd), 0, "");
        ASSERT_EQ(unlink(info->path), 0, "");
    }
    ASSERT_EQ(EndBlobstoreTest(ramdisk_path), 0, "unmounting blobstore");
    END_TEST;
}

static bool TestReaddir(void) {
    BEGIN_TEST;
    char ramdisk_path[PATH_MAX];
    ASSERT_EQ(StartBlobstoreTest(512, 1 << 20, ramdisk_path), 0, "Mounting Blobstore");

    constexpr size_t kMaxEntries = 50;
    constexpr size_t kBlobSize = 1 << 10;

    AllocChecker ac;
    mxtl::Array<mxtl::unique_ptr<blob_info_t>>
            info(new (&ac) mxtl::unique_ptr<blob_info_t>[kMaxEntries](), kMaxEntries);
    ASSERT_TRUE(ac.check(), "");

    // Try to readdir on an empty directory
    DIR* dir = opendir(MOUNT_PATH);
    ASSERT_NONNULL(dir, "");
    ASSERT_NULL(readdir(dir), "Expected blobstore to start empty");

    // Fill a directory with entries
    for (size_t i = 0; i < kMaxEntries; i++) {
        ASSERT_TRUE(GenerateBlob(kBlobSize, &info[i]), "");
        int fd;
        ASSERT_TRUE(MakeBlob(info[i]->path, info[i]->merkle.get(), info[i]->size_merkle,
                             info[i]->data.get(), info[i]->size_data, &fd), "");
        ASSERT_EQ(close(fd), 0, "");
        fd = open(info[i]->path, O_RDWR);
        ASSERT_GT(fd, 0, "Failed to-reopen blob");
        ASSERT_TRUE(VerifyContents(fd, info[i]->data.get(), info[i]->size_data), "");
        ASSERT_EQ(close(fd), 0, "");
    }

    // Check that we see the expected number of entries
    size_t entries_seen = 0;
    struct dirent* de;
    while ((de = readdir(dir)) != nullptr) {
        entries_seen++;
    }
    ASSERT_EQ(entries_seen, kMaxEntries, "");
    entries_seen = 0;
    rewinddir(dir);

    // Readdir on a directory which contains entries, removing them as we go
    // along.
    while ((de = readdir(dir)) != nullptr) {
        for (size_t i = 0; i < kMaxEntries; i++) {
            if ((info[i]->size_data != 0) &&
                strcmp(strrchr(info[i]->path, '/') + 1, de->d_name) == 0) {
                ASSERT_EQ(unlink(info[i]->path), 0, "");
                // It's a bit hacky, but we set 'size_data' to zero
                // to identify the entry has been unlinked.
                info[i]->size_data = 0;
                goto found;
            }
        }
        ASSERT_TRUE(false, "Blobstore Readdir found an unexpected entry");
found:
        entries_seen++;
    }
    ASSERT_EQ(entries_seen, kMaxEntries, "");

    ASSERT_NULL(readdir(dir), "Expected blobstore to end empty");

    ASSERT_EQ(closedir(dir), 0, "");
    ASSERT_EQ(EndBlobstoreTest(ramdisk_path), 0, "unmounting blobstore");
    END_TEST;
}

static bool UseAfterUnlink(void) {
    BEGIN_TEST;
    char ramdisk_path[PATH_MAX];
    ASSERT_EQ(StartBlobstoreTest(512, 1 << 20, ramdisk_path), 0, "Mounting Blobstore");

    for (size_t i = 0; i < 16; i++) {
        mxtl::unique_ptr<blob_info_t> info;
        ASSERT_TRUE(GenerateBlob(1 << i, &info), "");

        int fd;
        ASSERT_TRUE(MakeBlob(info->path, info->merkle.get(), info->size_merkle,
                             info->data.get(), info->size_data, &fd), "");

        // We should be able to unlink the blob
        ASSERT_EQ(unlink(info->path), 0, "Failed to unlink");

        // We should still be able to read the blob after unlinking
        ASSERT_TRUE(VerifyContents(fd, info->data.get(), info->size_data), "");

        // After closing the fd, however, we should not be able to re-open the blob
        ASSERT_EQ(close(fd), 0, "");
        ASSERT_LT(open(info->path, O_RDWR), 0, "Expected blob to be deleted");
    }

    ASSERT_EQ(EndBlobstoreTest(ramdisk_path), 0, "unmounting blobstore");
    END_TEST;
}

static bool WriteAfterRead(void) {
    BEGIN_TEST;
    char ramdisk_path[PATH_MAX];
    ASSERT_EQ(StartBlobstoreTest(512, 1 << 20, ramdisk_path), 0, "Mounting Blobstore");

    for (size_t i = 0; i < 16; i++) {
        mxtl::unique_ptr<blob_info_t> info;
        ASSERT_TRUE(GenerateBlob(1 << i, &info), "");

        int fd;
        ASSERT_TRUE(MakeBlob(info->path, info->merkle.get(), info->size_merkle,
                             info->data.get(), info->size_data, &fd), "");

        // After blob generation, writes should be rejected
        ASSERT_LT(write(fd, info->data.get(), 1), 0,
                  "After being written, the blob should refuse writes");

        off_t seek_pos = (rand() % info->size_data);
        ASSERT_EQ(lseek(fd, seek_pos, SEEK_SET), seek_pos, "");
        ASSERT_LT(write(fd, info->data.get(), 1), 0,
                  "After being written, the blob should refuse writes");
        ASSERT_LT(ftruncate(fd, rand() % info->size_data), 0,
                  "The blob should always refuse to be truncated");

        // We should be able to unlink the blob
        ASSERT_EQ(close(fd), 0, "");
        ASSERT_EQ(unlink(info->path), 0, "Failed to unlink");
    }

    ASSERT_EQ(EndBlobstoreTest(ramdisk_path), 0, "unmounting blobstore");
    END_TEST;
}

static bool BadAllocation(void) {
    BEGIN_TEST;
    char ramdisk_path[PATH_MAX];
    ASSERT_EQ(StartBlobstoreTest(512, 1 << 20, ramdisk_path), 0, "Mounting Blobstore");

    ASSERT_LT(open(MOUNT_PATH "/00112233445566778899AABBCCDDEEFFGGHHIIJJKKLLMMNNOOPPQQRRSSTTUUVV",
                   O_CREAT | O_RDWR), 0, "Only acceptable pathnames are hex");
    ASSERT_LT(open(MOUNT_PATH "/00112233445566778899AABBCCDDEEFF", O_CREAT | O_RDWR), 0,
              "Only acceptable pathnames are 32 hex-encoded bytes");

    mxtl::unique_ptr<blob_info_t> info;
    ASSERT_TRUE(GenerateBlob(1 << 15, &info), "");

    int fd = open(info->path, O_CREAT | O_RDWR);
    ASSERT_GT(fd, 0, "Failed to create blob");
    ASSERT_EQ(ftruncate(fd, 0), -1, "Blob without data");
    // This is the size of the entire disk; we won't have room.
    ASSERT_EQ(ftruncate(fd, (1 << 20) * 512), -1, "Huge blob");

    // Okay, finally, a valid blob!
    ASSERT_EQ(ftruncate(fd, info->size_data), 0, "Failed to allocate blob");

    // Write nothing, but close the blob. Since the write was incomplete,
    // it will be inaccessible.
    ASSERT_EQ(close(fd), 0, "");
    ASSERT_LT(open(info->path, O_RDWR), 0, "Cannot access partial blob");

    // And once more -- let's write everything but the last byte of a blob's data.
    fd = open(info->path, O_CREAT | O_RDWR);
    ASSERT_GT(fd, 0, "Failed to create blob");
    ASSERT_EQ(ftruncate(fd, info->size_data), 0, "Failed to allocate blob");
    ASSERT_EQ(StreamAll(write, fd, info->data.get(), info->size_data - 1), 0,
              "Failed to write data");
    ASSERT_EQ(close(fd), 0, "");
    ASSERT_LT(open(info->path, O_RDWR), 0, "Cannot access partial blob");

    ASSERT_EQ(EndBlobstoreTest(ramdisk_path), 0, "unmounting blobstore");
    END_TEST;
}

static bool CorruptedBlob(void) {
    BEGIN_TEST;
    char ramdisk_path[PATH_MAX];
    ASSERT_EQ(StartBlobstoreTest(512, 1 << 20, ramdisk_path), 0, "Mounting Blobstore");

    mxtl::unique_ptr<blob_info_t> info;
    for (size_t i = 1; i < 18; i++) {
        ASSERT_TRUE(GenerateBlob(1 << i, &info), "");
        info->size_data -= (rand() % info->size_data) + 1;
        if (info->size_data == 0) {
            info->size_data = 1;
        }
        ASSERT_TRUE(MakeBlobCompromised(info->path, info->merkle.get(),
                                        info->size_merkle, info->data.get(),
                                        info->size_data), "");
    }

    for (size_t i = 0; i < 18; i++) {
        ASSERT_TRUE(GenerateBlob(1 << i, &info), "");
        // Flip a random bit of the data
        size_t rand_index = rand() % info->size_data;
        char old_val = info->data.get()[rand_index];
        while ((info->data.get()[rand_index] = static_cast<char>(rand())) == old_val) {}
        ASSERT_TRUE(MakeBlobCompromised(info->path, info->merkle.get(),
                                        info->size_merkle, info->data.get(),
                                        info->size_data), "");
    }

    ASSERT_EQ(EndBlobstoreTest(ramdisk_path), 0, "unmounting blobstore");
    END_TEST;
}

static bool CorruptedDigest(void) {
    BEGIN_TEST;
    char ramdisk_path[PATH_MAX];
    ASSERT_EQ(StartBlobstoreTest(512, 1 << 20, ramdisk_path), 0, "Mounting Blobstore");

    mxtl::unique_ptr<blob_info_t> info;
    for (size_t i = 1; i < 18; i++) {
        ASSERT_TRUE(GenerateBlob(1 << i, &info), "");

        char hexdigits[17] = "0123456789abcdef";
        size_t idx = strlen(info->path) - 1 - (rand() % (2 * merkle::Digest::kLength));
        char newchar = hexdigits[rand() % 16];
        while (info->path[idx] == newchar) {
            newchar = hexdigits[rand() % 16];
        }
        info->path[idx] = newchar;
        ASSERT_TRUE(MakeBlobCompromised(info->path, info->merkle.get(),
                                        info->size_merkle, info->data.get(),
                                        info->size_data), "");
    }

    for (size_t i = 0; i < 18; i++) {
        ASSERT_TRUE(GenerateBlob(1 << i, &info), "");
        // Flip a random bit of the data
        size_t rand_index = rand() % info->size_data;
        char old_val = info->data.get()[rand_index];
        while ((info->data.get()[rand_index] = static_cast<char>(rand())) == old_val) {}
        ASSERT_TRUE(MakeBlobCompromised(info->path, info->merkle.get(),
                                        info->size_merkle, info->data.get(),
                                        info->size_data), "");
    }

    ASSERT_EQ(EndBlobstoreTest(ramdisk_path), 0, "unmounting blobstore");
    END_TEST;
}

static bool EdgeAllocation(void) {
    BEGIN_TEST;
    char ramdisk_path[PATH_MAX];
    ASSERT_EQ(StartBlobstoreTest(512, 1 << 20, ramdisk_path), 0, "Mounting Blobstore");

    // Powers of two...
    for (size_t i = 1; i < 16; i++) {
        // -1, 0, +1 offsets...
        for (size_t j = -1; j < 2; j++) {
            mxtl::unique_ptr<blob_info_t> info;
            ASSERT_TRUE(GenerateBlob((1 << i) + j, &info), "");
            int fd;
            ASSERT_TRUE(MakeBlob(info->path, info->merkle.get(), info->size_merkle,
                                 info->data.get(), info->size_data, &fd), "");
            ASSERT_EQ(unlink(info->path), 0, "");
            ASSERT_EQ(close(fd), 0, "");
        }
    }
    ASSERT_EQ(EndBlobstoreTest(ramdisk_path), 0, "unmounting blobstore");
    END_TEST;
}

static bool CreateUmountRemountSmall(void) {
    BEGIN_TEST;
    char ramdisk_path[PATH_MAX];
    ASSERT_EQ(StartBlobstoreTest(512, 1 << 20, ramdisk_path), 0, "Mounting Blobstore");

    for (size_t i = 10; i < 16; i++) {
        mxtl::unique_ptr<blob_info_t> info;
        ASSERT_TRUE(GenerateBlob(1 << i, &info), "");

        int fd;
        ASSERT_TRUE(MakeBlob(info->path, info->merkle.get(), info->size_merkle,
                             info->data.get(), info->size_data, &fd), "");
        // Close fd, unmount filesystem
        ASSERT_EQ(close(fd), 0, "");
        ASSERT_EQ(umount(MOUNT_PATH), NO_ERROR, "Could not unmount blobstore");
        ASSERT_EQ(MountBlobstore(ramdisk_path), 0, "Could not re-mount blobstore");

        fd = open(info->path, O_RDWR);
        ASSERT_GT(fd, 0, "Failed to open blob");

        ASSERT_TRUE(VerifyContents(fd, info->data.get(), info->size_data), "");
        ASSERT_EQ(close(fd), 0, "Could not close blob");
        ASSERT_EQ(unlink(info->path), 0, "");
    }

    ASSERT_EQ(EndBlobstoreTest(ramdisk_path), 0, "unmounting blobstore");
    END_TEST;
}

enum TestState {
    empty,
    configured,
    readable,
};

typedef struct blob_state : public mxtl::DoublyLinkedListable<mxtl::unique_ptr<blob_state>> {
    blob_state(mxtl::unique_ptr<blob_info_t> i) : info(mxtl::move(i)), state(empty) {}

    mxtl::unique_ptr<blob_info_t> info;
    TestState state;
    int fd;
} blob_state_t;

typedef struct blob_list {
    mxtl::Mutex list_lock;
    mxtl::DoublyLinkedList<mxtl::unique_ptr<blob_state>> list;
} blob_list_t;

// Generate and open a new blob
bool blob_create_helper(blob_list_t* bl, unsigned* seed) {
    mxtl::unique_ptr<blob_info_t> info;
    ASSERT_TRUE(GenerateBlob(1 + (rand_r(seed) % (1 << 16)), &info), "");

    AllocChecker ac;
    mxtl::unique_ptr<blob_state_t> state(new (&ac) blob_state(mxtl::move(info)));
    ASSERT_EQ(ac.check(), true, "");

    int fd = open(state->info->path, O_CREAT | O_RDWR);
    ASSERT_GT(fd, 0, "Failed to create blob");
    state->fd = fd;
    {
        mxtl::AutoLock al(&bl->list_lock);
        bl->list.push_front(mxtl::move(state));
    }
    return true;
}

// Allocate space for an open, empty blob
bool blob_config_helper(blob_list_t* bl) {
    mxtl::unique_ptr<blob_state> state;
    {
        mxtl::AutoLock al(&bl->list_lock);
        state = bl->list.pop_back();
    }

    if (state == nullptr) {
        return true;
    } else if (state->state == empty) {
        ASSERT_EQ(ftruncate(state->fd, state->info->size_data), 0, "");
        state->state = configured;
    }
    {
        mxtl::AutoLock al(&bl->list_lock);
        bl->list.push_front(mxtl::move(state));
    }
    return true;
}

// Write the data for an open, partially written blob
bool blob_write_data_helper(blob_list_t* bl) {
    mxtl::unique_ptr<blob_state> state;
    {
        mxtl::AutoLock al(&bl->list_lock);
        state = bl->list.pop_back();
    }
    if (state == nullptr) {
        return true;
    } else if (state->state == configured) {
        ASSERT_EQ(StreamAll(write, state->fd, state->info->data.get(),
                            state->info->size_data), 0, "Failed to write Data");
        state->state = readable;
    }
    {
        mxtl::AutoLock al(&bl->list_lock);
        bl->list.push_front(mxtl::move(state));
    }
    return true;
}

// Read the blob's data
bool blob_read_data_helper(blob_list_t* bl) {
    mxtl::unique_ptr<blob_state> state;
    {
        mxtl::AutoLock al(&bl->list_lock);
        state = bl->list.pop_back();
    }
    if (state == nullptr) {
        return true;
    } else if (state->state == readable) {
        ASSERT_TRUE(VerifyContents(state->fd, state->info->data.get(),
                                   state->info->size_data), "");
    }
    {
        mxtl::AutoLock al(&bl->list_lock);
        bl->list.push_front(mxtl::move(state));
    }
    return true;
}

// Unlink the blob
auto blob_unlink_helper(blob_list_t* bl) {
    mxtl::unique_ptr<blob_state> state;
    {
        mxtl::AutoLock al(&bl->list_lock);
        state = bl->list.pop_back();
    }
    if (state == nullptr) {
        return true;
    }
    ASSERT_EQ(unlink(state->info->path), 0, "Could not unlink blob");
    ASSERT_EQ(close(state->fd), 0, "Could not close blob");
    return true;
}

static bool CreateUmountRemountLarge(void) {
    BEGIN_TEST;
    char ramdisk_path[PATH_MAX];
    ASSERT_EQ(StartBlobstoreTest(512, 1 << 20, ramdisk_path), 0, "Mounting Blobstore");

    blob_list_t bl;
    // TODO(smklein): Here, and elsewhere in this file, remove this source
    // of randomness to make the unit test deterministic -- fuzzing should
    // be the tool responsible for introducing randomness into the system.
    unsigned int seed = static_cast<unsigned int>(mx_ticks_get());
    unittest_printf("unmount_remount test using seed: %u\n", seed);

    // Do some operations...
    size_t num_ops = 5000;
    for (size_t i = 0; i < num_ops; ++i) {
        switch (rand_r(&seed) % 5) {
        case 0: ASSERT_TRUE(blob_create_helper(&bl, &seed), "");       break;
        case 1: ASSERT_TRUE(blob_config_helper(&bl), "");       break;
        case 2: ASSERT_TRUE(blob_write_data_helper(&bl), "");   break;
        case 3: ASSERT_TRUE(blob_read_data_helper(&bl), "");    break;
        case 4: ASSERT_TRUE(blob_unlink_helper(&bl), "");       break;
        }
    }

    // Close all currently opened nodes (REGARDLESS of their state)
    for (auto& state: bl.list) {
        ASSERT_EQ(close(state.fd), 0, "");
    }

    // Unmount, remount
    ASSERT_EQ(umount(MOUNT_PATH), NO_ERROR, "Could not unmount blobstore");
    ASSERT_EQ(MountBlobstore(ramdisk_path), 0, "Could not re-mount blobstore");

    for (auto& state: bl.list) {
        if (state.state == readable) {
            // If a blob was readable before being unmounted, it should still exist.
            int fd = open(state.info->path, O_RDWR);
            ASSERT_GT(fd, 0, "Failed to create blob");
            ASSERT_TRUE(VerifyContents(fd, state.info->data.get(),
                                       state.info->size_data), "");
            ASSERT_EQ(unlink(state.info->path), 0, "");
            ASSERT_EQ(close(fd), 0, "");
        } else {
            // ... otherwise, the blob should have been deleted.
            ASSERT_LT(open(state.info->path, O_RDWR), 0, "");
        }
    }

    ASSERT_EQ(EndBlobstoreTest(ramdisk_path), 0, "unmounting blobstore");
    END_TEST;
}

int unmount_remount_thread(void* arg) {
    blob_list_t* bl = static_cast<blob_list_t*>(arg);
    unsigned int seed = static_cast<unsigned int>(mx_ticks_get());
    unittest_printf("unmount_remount thread using seed: %u\n", seed);

    // Do some operations...
    size_t num_ops = 1000;
    for (size_t i = 0; i < num_ops; ++i) {
        switch (rand_r(&seed) % 5) {
        case 0: ASSERT_TRUE(blob_create_helper(bl, &seed), "");       break;
        case 1: ASSERT_TRUE(blob_config_helper(bl), "");       break;
        case 2: ASSERT_TRUE(blob_write_data_helper(bl), "");   break;
        case 3: ASSERT_TRUE(blob_read_data_helper(bl), "");    break;
        case 4: ASSERT_TRUE(blob_unlink_helper(bl), "");       break;
        }
    }

    return 0;
}

static bool CreateUmountRemountLargeMultithreaded(void) {
    BEGIN_TEST;
    char ramdisk_path[PATH_MAX];
    ASSERT_EQ(StartBlobstoreTest(512, 1 << 20, ramdisk_path), 0, "Mounting Blobstore");

    blob_list_t bl;

    size_t num_threads = 10;
    AllocChecker ac;
    mxtl::Array<thrd_t> threads(new (&ac) thrd_t[num_threads](), num_threads);
    ASSERT_TRUE(ac.check(), "");

    // Launch all threads
    for (size_t i = 0; i < num_threads; i++ ) {
        ASSERT_EQ(thrd_create(&threads[i], unmount_remount_thread, &bl),
                  thrd_success, "");
    }

    // Wait for all threads to complete
    for (size_t i = 0; i < num_threads; i++ ) {
        int res;
        ASSERT_EQ(thrd_join(threads[i], &res), thrd_success, "");
        ASSERT_EQ(res, 0, "");
    }

    // Close all currently opened nodes (REGARDLESS of their state)
    for (auto& state: bl.list) {
        ASSERT_EQ(close(state.fd), 0, "");
    }

    // Unmount, remount
    ASSERT_EQ(umount(MOUNT_PATH), NO_ERROR, "Could not unmount blobstore");
    ASSERT_EQ(MountBlobstore(ramdisk_path), 0, "Could not re-mount blobstore");

    for (auto& state: bl.list) {
        if (state.state == readable) {
            // If a blob was readable before being unmounted, it should still exist.
            int fd = open(state.info->path, O_RDWR);
            ASSERT_GT(fd, 0, "Failed to create blob");
            ASSERT_TRUE(VerifyContents(fd, state.info->data.get(),
                                       state.info->size_data), "");
            ASSERT_EQ(unlink(state.info->path), 0, "");
            ASSERT_EQ(close(fd), 0, "");
        } else {
            // ... otherwise, the blob should have been deleted.
            ASSERT_LT(open(state.info->path, O_RDWR), 0, "");
        }
    }

    ASSERT_EQ(EndBlobstoreTest(ramdisk_path), 0, "unmounting blobstore");
    END_TEST;
}

static bool NoSpace(void) {
    BEGIN_TEST;
    char ramdisk_path[PATH_MAX];
    ASSERT_EQ(StartBlobstoreTest(512, 1 << 20, ramdisk_path), 0, "Mounting Blobstore");

    mxtl::unique_ptr<blob_info_t> last_info = nullptr;

    // Keep generating blobs until we run out of space
    size_t count = 0;
    while (true) {
        mxtl::unique_ptr<blob_info_t> info;
        ASSERT_TRUE(GenerateBlob(1 << 17, &info), "");

        int fd = open(info->path, O_CREAT | O_RDWR);
        ASSERT_GT(fd, 0, "Failed to create blob");
        int r = ftruncate(fd, info->size_data);
        if (r < 0) {
            ASSERT_EQ(errno, ENOSPC, "Blobstore expected to run out of space");
            // We ran out of space, as expected. Can we allocate if we
            // unlink a previously allocated blob of the desired size?
            ASSERT_EQ(unlink(last_info->path), 0, "Unlinking old blob");
            ASSERT_EQ(ftruncate(fd, info->size_data), 0, "Re-init after unlink");

            // Yay! allocated successfully.
            ASSERT_EQ(close(fd), 0, "");
            break;
        }
        ASSERT_EQ(StreamAll(write, fd, info->data.get(), info->size_data), 0,
                  "Failed to write Data");
        ASSERT_EQ(close(fd), 0, "");
        last_info = mxtl::move(info);

        if (++count % 100 == 0) {
            printf("Allocated %lu blobs\n", count);
        }
    }

    ASSERT_EQ(EndBlobstoreTest(ramdisk_path), 0, "unmounting blobstore");
    END_TEST;
}

static bool check_not_readable(int fd) {
    struct pollfd fds;
    fds.fd = fd;
    fds.events = POLLIN;
    ASSERT_EQ(poll(&fds, 1, 10), 0, "Failed to wait for readable blob");

    char buf[8];
    ASSERT_EQ(lseek(fd, 0, SEEK_SET), 0, "");
    ASSERT_LT(read(fd, buf, 1), 0, "Blob should not be readable yet");
    return true;
}

static bool wait_readable(int fd) {
    struct pollfd fds;
    fds.fd = fd;
    fds.events = POLLIN;
    ASSERT_EQ(poll(&fds, 1, 10000), 1, "Failed to wait for readable blob");
    ASSERT_EQ(fds.revents, POLLIN, "");

    return true;
}

static bool check_readable(int fd) {
    struct pollfd fds;
    fds.fd = fd;
    fds.events = POLLIN;
    ASSERT_EQ(poll(&fds, 1, 10), 1, "Failed to wait for readable blob");
    ASSERT_EQ(fds.revents, POLLIN, "");

    char buf[8];
    ASSERT_EQ(lseek(fd, 0, SEEK_SET), 0, "");
    ASSERT_EQ(read(fd, buf, sizeof(buf)), sizeof(buf), "");
    return true;
}

static bool EarlyRead(void) {
    // Check that we cannot read from the Blob until it has been fully written
    BEGIN_TEST;
    char ramdisk_path[PATH_MAX];
    ASSERT_EQ(StartBlobstoreTest(512, 1 << 20, ramdisk_path), 0, "Mounting Blobstore");

    mxtl::unique_ptr<blob_info_t> info;

    ASSERT_TRUE(GenerateBlob(1 << 17, &info), "");
    int fd = open(info->path, O_CREAT | O_EXCL | O_RDWR);
    ASSERT_GT(fd, 0, "Failed to create blob");

    ASSERT_LT(open(info->path, O_CREAT | O_EXCL | O_RDWR), 0,
              "Should not be able to exclusively create twice");

    // This second fd should also not be readable
    int fd2 = open(info->path, O_CREAT | O_RDWR);
    ASSERT_GT(fd2, 0, "Failed to create blob");

    ASSERT_TRUE(check_not_readable(fd), "Should not be readable after open");
    ASSERT_TRUE(check_not_readable(fd2), "Should not be readable after open");
    ASSERT_EQ(ftruncate(fd, info->size_data), 0, "");
    ASSERT_TRUE(check_not_readable(fd), "Should not be readable after alloc");
    ASSERT_TRUE(check_not_readable(fd2), "Should not be readable after alloc");
    ASSERT_EQ(StreamAll(write, fd, info->data.get(), info->size_data), 0,
              "Failed to write Data");

    // Okay, NOW we can read.
    // Double check that attempting to read early didn't cause problems...
    ASSERT_TRUE(VerifyContents(fd, info->data.get(), info->size_data), "");
    ASSERT_TRUE(VerifyContents(fd2, info->data.get(), info->size_data), "");

    // Cool, everything is readable. What if we try accessing the blob status now?
    EXPECT_TRUE(check_readable(fd), "");

    ASSERT_EQ(close(fd), 0, "");
    ASSERT_EQ(close(fd2), 0, "");
    ASSERT_EQ(unlink(info->path), 0, "");

    ASSERT_EQ(EndBlobstoreTest(ramdisk_path), 0, "unmounting blobstore");
    END_TEST;
}

static bool WaitForRead(void) {
    // Check that we cannot read from the Blob until it has been fully written
    BEGIN_TEST;
    char ramdisk_path[PATH_MAX];
    ASSERT_EQ(StartBlobstoreTest(512, 1 << 20, ramdisk_path), 0, "Mounting Blobstore");

    mxtl::unique_ptr<blob_info_t> info;

    ASSERT_TRUE(GenerateBlob(1 << 17, &info), "");
    int fd = open(info->path, O_CREAT | O_EXCL | O_RDWR);
    ASSERT_GT(fd, 0, "Failed to create blob");

    ASSERT_LT(open(info->path, O_CREAT | O_EXCL | O_RDWR), 0,
              "Should not be able to exclusively create twice");

    // Launch a background thread to wait for fd to become readable
    auto wait_until_readable = [](void* arg) {
        int fd = *static_cast<int*>(arg);
        EXPECT_TRUE(wait_readable(fd), "");
        EXPECT_TRUE(check_readable(fd), "");
        EXPECT_EQ(close(fd), 0, "");
        return 0;
    };
    int dupfd = dup(fd);
    ASSERT_GT(dupfd, 0, "Could not dup fd");
    thrd_t waiter_thread;
    thrd_create(&waiter_thread, wait_until_readable, &dupfd);

    ASSERT_TRUE(check_not_readable(fd), "Should not be readable after open");
    ASSERT_EQ(ftruncate(fd, info->size_data), 0, "");
    ASSERT_TRUE(check_not_readable(fd), "Should not be readable after alloc");
    ASSERT_EQ(StreamAll(write, fd, info->data.get(), info->size_data), 0,
              "Failed to write Data");

    // Cool, everything is readable. What if we try accessing the blob status now?
    EXPECT_TRUE(check_readable(fd), "");

    // Our background thread should have also completed successfully...
    int result;
    ASSERT_EQ(thrd_join(waiter_thread, &result), 0, "thrd_join failed");
    ASSERT_EQ(result, 0, "Unexpected result from background thread");

    // Double check that attempting to read early didn't cause problems...
    ASSERT_TRUE(VerifyContents(fd, info->data.get(), info->size_data), "");
    ASSERT_EQ(close(fd), 0, "");
    ASSERT_EQ(unlink(info->path), 0, "");

    ASSERT_EQ(EndBlobstoreTest(ramdisk_path), 0, "unmounting blobstore");
    END_TEST;
}

static bool WriteSeekIgnored(void) {
    // Check that seeks during writing are ignored
    BEGIN_TEST;
    char ramdisk_path[PATH_MAX];
    ASSERT_EQ(StartBlobstoreTest(512, 1 << 20, ramdisk_path), 0, "Mounting Blobstore");

    mxtl::unique_ptr<blob_info_t> info;
    ASSERT_TRUE(GenerateBlob(1 << 17, &info), "");
    int fd = open(info->path, O_CREAT | O_RDWR);
    ASSERT_GT(fd, 0, "Failed to create blob");
    ASSERT_EQ(ftruncate(fd, info->size_data), 0, "");

    size_t n = 0;
    while (n != info->size_data) {
        off_t seek_pos = (rand() % info->size_data);
        ASSERT_EQ(lseek(fd, seek_pos, SEEK_SET), seek_pos, "");
        ssize_t d = write(fd, info->data.get(), info->size_data - n);
        ASSERT_GT(d, 0, "Data Write error");
        n += d;
    }

    // Double check that attempting to seek early didn't cause problems...
    ASSERT_TRUE(VerifyContents(fd, info->data.get(), info->size_data), "");
    ASSERT_EQ(close(fd), 0, "");
    ASSERT_EQ(unlink(info->path), 0, "");

    ASSERT_EQ(EndBlobstoreTest(ramdisk_path), 0, "unmounting blobstore");
    END_TEST;
}

static bool UnlinkTiming(void) {
    // Try unlinking at a variety of times
    BEGIN_TEST;
    char ramdisk_path[PATH_MAX];
    ASSERT_EQ(StartBlobstoreTest(512, 1 << 20, ramdisk_path), 0, "Mounting Blobstore");

    // Unlink, close fd, re-open fd as new file
    auto full_unlink_reopen = [](int& fd, const char* path) {
        ASSERT_EQ(unlink(path), 0, "");
        ASSERT_EQ(close(fd), 0, "");
        fd = open(path, O_CREAT | O_RDWR | O_EXCL);
        ASSERT_GT(fd, 0, "Failed to recreate blob");
        return true;
    };

    mxtl::unique_ptr<blob_info_t> info;
    ASSERT_TRUE(GenerateBlob(1 << 17, &info), "");

    int fd = open(info->path, O_CREAT | O_RDWR);
    ASSERT_GT(fd, 0, "Failed to create blob");
    // Unlink after first open
    ASSERT_TRUE(full_unlink_reopen(fd, info->path), "");

    // Unlink after init
    ASSERT_EQ(ftruncate(fd, info->size_data), 0, "");
    ASSERT_TRUE(full_unlink_reopen(fd, info->path), "");

    // Unlink after first write
    ASSERT_EQ(ftruncate(fd, info->size_data), 0, "");
    ASSERT_EQ(StreamAll(write, fd, info->data.get(), info->size_data), 0,
              "Failed to write Data");
    ASSERT_TRUE(full_unlink_reopen(fd, info->path), "");
    ASSERT_EQ(unlink(info->path), 0, "");
    ASSERT_EQ(close(fd), 0, "");
    ASSERT_EQ(EndBlobstoreTest(ramdisk_path), 0, "unmounting blobstore");
    END_TEST;
}

static bool InvalidOps(void) {
    // Attempt using invalid operations
    BEGIN_TEST;
    char ramdisk_path[PATH_MAX];
    ASSERT_EQ(StartBlobstoreTest(512, 1 << 20, ramdisk_path), 0, "Mounting Blobstore");

    // First off, make a valid blob
    mxtl::unique_ptr<blob_info_t> info;
    ASSERT_TRUE(GenerateBlob(1 << 12, &info), "");
    int fd;
    ASSERT_TRUE(MakeBlob(info->path, info->merkle.get(), info->size_merkle,
                         info->data.get(), info->size_data, &fd), "");
    ASSERT_TRUE(VerifyContents(fd, info->data.get(), info->size_data), "");

    // Neat. Now, let's try some unsupported operations
    ASSERT_LT(rename(info->path, info->path), 0, "");
    ASSERT_LT(truncate(info->path, 0), 0, "");
    ASSERT_LT(utime(info->path, nullptr), 0, "");

    // TODO(smklein): Test that a blob fd cannot unmount the entire blobstore.
    //    ASSERT_LT(ioctl_vfs_unmount_fs(fd), 0, "");

    // Access the file once more, after these operations
    ASSERT_TRUE(VerifyContents(fd, info->data.get(), info->size_data), "");
    ASSERT_EQ(unlink(info->path), 0, "");
    ASSERT_EQ(close(fd), 0, "");

    ASSERT_EQ(EndBlobstoreTest(ramdisk_path), 0, "unmounting blobstore");
    END_TEST;
}

static bool RootDirectory(void) {
    // Attempt operations on the root directory
    BEGIN_TEST;
    char ramdisk_path[PATH_MAX];
    ASSERT_EQ(StartBlobstoreTest(512, 1 << 20, ramdisk_path), 0, "Mounting Blobstore");

    int dirfd = open(MOUNT_PATH "/.", O_RDWR);
    ASSERT_GT(dirfd, 0, "Cannot open root directory");

    mxtl::unique_ptr<blob_info_t> info;
    ASSERT_TRUE(GenerateBlob(1 << 12, &info), "");

    // Test ioctls which should ONLY operate on Blobs
    ASSERT_LT(ftruncate(dirfd, info->size_data), 0, "");

    // Should NOT be able to unlink root dir
    ASSERT_EQ(close(dirfd), 0, "");
    ASSERT_LT(unlink(info->path), 0, "");

    char buf[8];
    ASSERT_LT(write(dirfd, buf, 8), 0, "Should not write to directory");
    ASSERT_LT(read(dirfd, buf, 8), 0, "Should not read from directory");

    ASSERT_EQ(EndBlobstoreTest(ramdisk_path), 0, "unmounting blobstore");
    END_TEST;
}

BEGIN_TEST_CASE(blobstore_tests)
RUN_TEST_MEDIUM(TestBasic)
RUN_TEST_MEDIUM(TestMmap)
RUN_TEST_MEDIUM(TestReaddir)
RUN_TEST_MEDIUM(UseAfterUnlink)
RUN_TEST_MEDIUM(WriteAfterRead)
RUN_TEST_MEDIUM(BadAllocation)
RUN_TEST_MEDIUM(CorruptedBlob)
RUN_TEST_MEDIUM(CorruptedDigest)
RUN_TEST_MEDIUM(EdgeAllocation)
RUN_TEST_MEDIUM(CreateUmountRemountSmall)
RUN_TEST_MEDIUM(EarlyRead)
RUN_TEST_MEDIUM(WaitForRead)
RUN_TEST_MEDIUM(WriteSeekIgnored)
RUN_TEST_MEDIUM(UnlinkTiming)
RUN_TEST_MEDIUM(InvalidOps)
RUN_TEST_MEDIUM(RootDirectory)
RUN_TEST_LARGE(CreateUmountRemountLargeMultithreaded)
RUN_TEST_LARGE(CreateUmountRemountLarge)
RUN_TEST_LARGE(NoSpace)
END_TEST_CASE(blobstore_tests)

int main(int argc, char** argv) {
    return unittest_run_all_tests(argc, argv) ? 0 : -1;
}
