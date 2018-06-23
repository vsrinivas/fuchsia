// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file

#pragma once

#include <stdint.h>
#include <stdio.h>
#include <unistd.h>

#include <fbl/function.h>
#include <fbl/string.h>
#include <fbl/vector.h>
#include <fs-management/mount.h>
#include <fvm/fvm.h>
#include <zircon/status.h>
#include <zircon/syscalls.h>
#include <zircon/types.h>

// Macro for printing more information in error logs.
// "[File:Line] Error(error_name): Message\n"
#define LOG_ERROR(error_code, msg_fmt, ...)        \
    fprintf(stderr, "[%s:%d] Error(%s): " msg_fmt, \
            __FILE__, __LINE__, zx_status_get_string(error_code), ##__VA_ARGS__)

namespace fs_test_utils {

constexpr size_t kPathSize = PATH_MAX;

constexpr size_t kFvmBlockSize = FVM_BLOCK_SIZE;

// TODO(gevalentno): when ZX-2013 is resolved, make MemFs setup and teardown
// part of the test fixture and remove RunWithMemFs.
// Workaround that provides a MemFs per process, since it cannot be unbinded
// from the process namespace yet.
int RunWithMemFs(const fbl::Function<int()>& main_fn);

// Available options for the test fixture.
//
// Note: use_ramdisk and block_device_path are mutually exclusive.
struct FixtureOptions {

    static FixtureOptions Default(disk_format_t format) {
        FixtureOptions options;
        options.use_ramdisk = true;
        options.ramdisk_block_size = 512;
        options.ramdisk_block_count = zx_system_get_physmem() / (2 * options.ramdisk_block_size);
        options.use_fvm = false;
        options.fvm_slice_size = kFvmBlockSize * (2 << 10);
        options.fs_type = format;
        return options;
    }

    // Returns true if the options are valid.
    // When invalid |err_string| will be populated with a human readable error description.
    bool IsValid(fbl::String* err_description) const;

    // Path to the block device to use.
    fbl::String block_device_path = "";

    // If true a ramdisk will be created and shared for the test.
    bool use_ramdisk = false;

    // Number of blocks the ramdisk will contain.
    size_t ramdisk_block_count = 0;

    // Size of the blocks the ramdisk will have.
    size_t ramdisk_block_size = 0;

    // If true an fvm will be mounted on the device, and the filesystem will be
    // mounted on top of a fresh partition.
    bool use_fvm = false;

    // Size of each slice of the created fvm.
    size_t fvm_slice_size = 0;

    // Type of filesystem to mount.
    disk_format_t fs_type;
};

// Provides a base fixture for File system tests.
// In main(a.k.a run_all_unittests):
//
// RunWithMemFs([argc, argv] () {  // Sets up then cleans up Local MemFs.
//   return run_all_unittests(argc, argv) ? 0: 1;
// }
class Fixture {
public:
    Fixture() = delete;
    explicit Fixture(const FixtureOptions& options);
    Fixture(const Fixture&) = delete;
    Fixture(Fixture&&) = delete;
    Fixture& operator=(const Fixture&) = delete;
    Fixture& operator=(Fixture&&) = delete;
    ~Fixture();

    // Returns the options used by this fixture.
    const FixtureOptions& options() const {
        return options_;
    }

    // Returns the path to the block device hosting the FS.
    const fbl::String& block_device_path() const {
        return block_device_path_;
    }

    // Returns the path to the FVM partition created for the block device
    // hosting the FS. Will return empty if !options_.use_fvm.
    const fbl::String& partition_path() const {
        return partition_path_;
    }

    // Returns either the block_device path or partition_path if using fvm.
    const fbl::String& GetFsBlockDevice() const {
        return (options_.use_fvm) ? partition_path_ : block_device_path_;
    }

    // Returns the path where the filesystem was mounted.
    const fbl::String& fs_path() const {
        return fs_path_;
    }

    // Sets up MemFs and Ramdisk, allocating resources for the tests.
    zx_status_t SetUpTestCase();

    // Formats the block device with the required type, creates a fvm, and mounts
    // the fs.
    zx_status_t SetUp();

    // Cleans up the block device by reformatting it, destroys the fvm and
    // unmounts the fs.
    zx_status_t TearDown();

    // Destroys the ramdisk, MemFs will die with the process. This should be
    // called after all tests finished execution to free resources.
    zx_status_t TearDownTestCase();

private:
    FixtureOptions options_;

    // State of the resources allocated by the fixture.
    enum class ResourceState {
        kUnallocated,
        kAllocated,
        kFreed,
    };

    // Path to the block device hosting the mounted FS.
    fbl::String block_device_path_;

    // When using fvm, the FS will be mounted here.
    fbl::String partition_path_;

    // The root path where FS is mounted.
    fbl::String fs_path_;

    // Keep track of the resource allocation during the setup teardown process,
    // to avoid leaks, or unnecessary errors when trying to free resources, that
    // may have never been allocated in first place.
    ResourceState fs_state_ = ResourceState::kUnallocated;
    ResourceState fvm_state_ = ResourceState::kUnallocated;
    ResourceState ramdisk_state_ = ResourceState::kUnallocated;
};

} // namespace fs_test_utils
