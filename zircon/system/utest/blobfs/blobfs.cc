// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <assert.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <fuchsia/blobfs/c/fidl.h>
#include <fuchsia/device/c/fidl.h>
#include <fuchsia/device/llcpp/fidl.h>
#include <fuchsia/hardware/ramdisk/c/fidl.h>
#include <fuchsia/io/c/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fdio/fdio.h>
#include <lib/fdio/io.h>
#include <lib/fdio/unsafe.h>
#include <lib/fzl/fdio.h>
#include <lib/memfs/memfs.h>
#include <lib/zx/vmo.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <threads.h>
#include <utime.h>
#include <zircon/device/vfs.h>
#include <zircon/syscalls.h>

#include <atomic>
#include <memory>
#include <type_traits>
#include <utility>

#include <blobfs/common.h>
#include <blobfs/format.h>
#include <digest/digest.h>
#include <digest/merkle-tree.h>
#include <fbl/alloc_checker.h>
#include <fbl/auto_call.h>
#include <fbl/auto_lock.h>
#include <fbl/intrusive_double_list.h>
#include <fbl/string.h>
#include <fbl/unique_fd.h>
#include <fbl/vector.h>
#include <fs-management/fvm.h>
#include <fs-management/mount.h>
#include <fs-test-utils/blobfs/blobfs.h>
#include <fs-test-utils/blobfs/bloblist.h>
#include <fvm/format.h>
#include <ramdevice-client/ramdisk.h>
#include <unittest/unittest.h>

#include "blobfs-test.h"

#define TMPFS_PATH "/blobfs-tmp"
#define MOUNT_PATH "/blobfs-tmp/zircon-blobfs-test"

namespace {
using digest::Digest;

// Indicates whether we should enable ramdisk failure tests for the current test run.
bool gEnableRamdiskFailure = false;

// The maximum number of failure loops that should be tested. If set to 0, all of them will be run.
uint64_t gRamdiskFailureLoops = 0;

// Indicates whether we should enable the journal for the current test run.
bool gEnableJournal = true;

// Indicates whether we should enable the pager for the current test run.
bool gEnablePager = false;

// Information about the real disk which must be constructed at runtime, but which persists
// between tests.
bool gUseRealDisk = false;
struct real_disk_info {
  uint64_t blk_size;
  uint64_t blk_count;
  char disk_path[PATH_MAX];
} gRealDiskInfo;

static bool gEnableOutput = true;

static_assert(std::is_pod<real_disk_info>::value,
              "Global variables should contain exclusively POD"
              "data");

#define RUN_TEST_WRAPPER(test_size, test_name, test_type) \
  RUN_TEST_##test_size((TestWrapper<test_name, test_type>))

#define RUN_TEST_NORMAL(test_size, test_name) \
  RUN_TEST_WRAPPER(test_size, test_name, FsTestType::kNormal)

#define RUN_TEST_FVM(test_size, test_name) RUN_TEST_WRAPPER(test_size, test_name, FsTestType::kFvm)

#define RUN_TESTS(test_size, test_name) \
  RUN_TEST_NORMAL(test_size, test_name) \
  RUN_TEST_FVM(test_size, test_name)

#define RUN_TESTS_SILENT(test_size, test_name) \
  gEnableOutput = false;                       \
  RUN_TESTS(test_size, test_name)              \
  gEnableOutput = true;

// Defines a Blobfs test function which can be passed to the TestWrapper.
typedef bool (*TestFunction)(BlobfsTest* blobfsTest);

// Function which takes in print parameters and does nothing.
static void silent_printf(const char* line, int len, void* arg) {}

// A test wrapper which runs a Blobfs test. If the -f command line argument is used, the test will
// then be run the specified number of additional times, purposely causing the underlying ramdisk
// to fail at certain points.
template <TestFunction TestFunc, FsTestType TestType>
bool TestWrapper(void) {
  BEGIN_TEST;

  // Make sure that we are not testing ramdisk failures with a real disk.
  ASSERT_FALSE(gUseRealDisk && gEnableRamdiskFailure);

  BlobfsTest blobfsTest(TestType);
  blobfsTest.SetStdio(gEnableOutput);
  ASSERT_TRUE(blobfsTest.Init(), "Mounting Blobfs");

  if (gEnableRamdiskFailure) {
    // Sleep and re-wake the ramdisk to ensure that transaction counts have been reset.
    ASSERT_TRUE(blobfsTest.ToggleSleep());
    ASSERT_TRUE(blobfsTest.ToggleSleep());
  }

  // Run the test. This should pass.
  ASSERT_TRUE(TestFunc(&blobfsTest));

  // Get the total number of transactions required for this test.
  uint64_t block_count = 0;
  uint64_t interval = 1;
  uint64_t total = 0;

  if (gEnableRamdiskFailure) {
    // Based on the number of total blocks written and the user provided count of tests,
    // calculate the block interval to test with ramdisk failures.
    ASSERT_TRUE(blobfsTest.GetRamdiskCount(&block_count));

    if (gRamdiskFailureLoops && gRamdiskFailureLoops < block_count) {
      interval = block_count / gRamdiskFailureLoops;
    }
  }

  ASSERT_TRUE(blobfsTest.Teardown(), "Unmounting Blobfs");
  blobfsTest.SetStdio(false);

  // Run the test again, setting the ramdisk to fail after each |interval| blocks.
  for (uint64_t i = 1; i <= block_count; i += interval) {
    if (gRamdiskFailureLoops && total >= gRamdiskFailureLoops) {
      break;
    }

    if (total % 100 == 0) {
      printf("Running ramdisk failure test %" PRIu64 " / %" PRIu64 " (block %" PRIu64 " / %" PRIu64
             ")\n",
             total + 1, gRamdiskFailureLoops ? gRamdiskFailureLoops : block_count, i, block_count);
    }

    ASSERT_TRUE(blobfsTest.Reset());
    ASSERT_TRUE(blobfsTest.Init(), "Mounting Blobfs");
    ASSERT_TRUE(blobfsTest.ToggleSleep(i));

    // The following test run may fail, but since we don't care, silence any test output.
    unittest_set_output_function(silent_printf, nullptr);

    // We do not care whether the test itself fails or not - regardless, fsck should pass
    // (even if the most recent file system state has not been preserved).
    TestFunc(&blobfsTest);
    current_test_info->all_ok = true;

    ASSERT_TRUE(blobfsTest.ToggleSleep());

    // Restore the default output function.
    unittest_restore_output_function();

    // Forcibly unmount and remount the blobfs partition. With journaling enabled, any
    // inconsistencies should be resolved.
    zx_status_t fsck_result;
    ASSERT_TRUE(blobfsTest.ForceRemount(&fsck_result));

    if (fsck_result != ZX_OK) {
      printf("Detected disk corruption on test %" PRIu64 " / %" PRIu64 " (block %" PRIu64
             " / %" PRIu64 ")\n",
             total, gRamdiskFailureLoops ? gRamdiskFailureLoops : block_count, i, block_count);
    }
    // TODO: When we convert to zxtest, print the above error message within this
    // assertion.
    ASSERT_EQ(fsck_result, ZX_OK);

    // The fsck check during Teardown should verify that journal replay was successful.
    ASSERT_TRUE(blobfsTest.Teardown(), "Unmounting Blobfs");
    total += 1;
  }

  END_TEST;
}

#define FVM_DRIVER_LIB "/boot/driver/fvm.so"
#define STRLEN(s) (sizeof(s) / sizeof((s)[0]))

// FVM slice size used for tests
constexpr size_t kTestFvmSliceSize = blobfs::kBlobfsBlockSize;  // 8kb
// Minimum blobfs size required by CreateUmountRemountLargeMultithreaded test
constexpr size_t kBytesNormalMinimum = 5 * (1 << 20);  // 5mb
// Minimum blobfs size required by ResizePartition test
constexpr size_t kSliceBytesFvmMinimum = 507 * kTestFvmSliceSize;
constexpr size_t kTotalBytesFvmMinimum = fvm::MetadataSize(kSliceBytesFvmMinimum,
                                                           kTestFvmSliceSize) *
                                             2 +
                                         kSliceBytesFvmMinimum;  // ~8.5mb

constexpr uint8_t kTestUniqueGUID[] = {0xFF, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
                                       0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f};
constexpr uint8_t kTestPartGUID[] = {0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f,
                                     0xFF, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07};

const fsck_options_t test_fsck_options = {
    .verbose = false,
    .never_modify = true,
    .always_modify = false,
    .force = true,
    .apply_journal = true,
};

BlobfsTest::~BlobfsTest() {
  switch (state_) {
    case FsTestState::kMinimal:
    case FsTestState::kRunning:
    case FsTestState::kError:
      EXPECT_EQ(Teardown(), 0);
      break;
    default:
      break;
  }
}

bool BlobfsTest::Init(FsTestState state) {
  BEGIN_HELPER;
  ASSERT_EQ(state_, FsTestState::kInit);
  auto error = fbl::MakeAutoCall([this]() { state_ = FsTestState::kError; });

  ASSERT_TRUE(mkdir(MOUNT_PATH, 0755) == 0 || errno == EEXIST,
              "Could not create mount point for test filesystems");

  if (gUseRealDisk) {
    strncpy(device_path_, gRealDiskInfo.disk_path, PATH_MAX);
    blk_size_ = gRealDiskInfo.blk_size;
    blk_count_ = gRealDiskInfo.blk_count;
  } else {
    ASSERT_EQ(ramdisk_create(blk_size_, blk_count_, &ramdisk_), ZX_OK,
              "Blobfs: Could not create ramdisk");
    strlcpy(device_path_, ramdisk_get_path(ramdisk_), sizeof(device_path_));
  }

  if (type_ == FsTestType::kFvm) {
    ASSERT_EQ(kTestFvmSliceSize % blobfs::kBlobfsBlockSize, 0);

    fbl::unique_fd fd(open(device_path_, O_RDWR));
    ASSERT_TRUE(fd, "[FAILED]: Could not open test disk");
    ASSERT_EQ(fvm_init(fd.get(), kTestFvmSliceSize), ZX_OK,
              "[FAILED]: Could not format disk with FVM");
    fzl::FdioCaller caller(std::move(fd));
    zx_status_t status = ZX_OK;
    auto resp = ::llcpp::fuchsia::device::Controller::Call::Bind(
        zx::unowned_channel(caller.borrow_channel()),
        ::fidl::StringView(FVM_DRIVER_LIB, STRLEN(FVM_DRIVER_LIB)));
    zx_status_t io_status = resp.status();
    ASSERT_EQ(io_status, ZX_OK, "[FAILED]: Could not send bind to FVM driver");
    if (resp->result.is_err()) {
      status = resp->result.err();
    }
    // TODO(fxb/39460) Prevent ALREADY_BOUND from being an option
    if (!(status == ZX_OK || status == ZX_ERR_ALREADY_BOUND)) {
      ASSERT_TRUE(false, "[FAILED] Driver wasn't already bound or failed to bind");
    }
    caller.reset();

    snprintf(fvm_path_, sizeof(fvm_path_), "%s/fvm", device_path_);
    ASSERT_EQ(wait_for_device(fvm_path_, zx::sec(10).get()), ZX_OK,
              "[FAILED]: FVM driver never appeared");

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
    fd.reset(open_partition(kTestUniqueGUID, kTestPartGUID, 0, device_path_));
    ASSERT_TRUE(fd, "[FAILED]: Could not locate FVM partition");
    fd.reset();
  }

  if (state != FsTestState::kMinimal) {
    ASSERT_EQ(state, FsTestState::kRunning);
    ASSERT_EQ(mkfs(device_path_, DISK_FORMAT_BLOBFS, launch_stdio_sync, &default_mkfs_options),
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
  auto error = fbl::MakeAutoCall([this]() { state_ = FsTestState::kError; });
  ASSERT_EQ(umount(MOUNT_PATH), ZX_OK, "Failed to unmount blobfs");
  LaunchCallback launch = stdio_ ? launch_stdio_sync : launch_silent_sync;
  ASSERT_EQ(fsck(device_path_, DISK_FORMAT_BLOBFS, &test_fsck_options, launch), ZX_OK,
            "Filesystem fsck failed");
  ASSERT_TRUE(Mount(), "Failed to mount blobfs");
  error.cancel();
  END_HELPER;
}

bool BlobfsTest::ForceRemount(zx_status_t* fsck_result) {
  BEGIN_HELPER;
  // Attempt to unmount, but do not check the result.
  // It is possible that the partition has already been unmounted.
  umount(MOUNT_PATH);

  if (fsck_result != nullptr) {
    *fsck_result = fsck(device_path_, DISK_FORMAT_BLOBFS, &test_fsck_options, launch_silent_sync);
  }

  ASSERT_TRUE(Mount());

  // In the event of success, set state to kRunning, regardless of whether the state was kMinimal
  // before. Since the partition is now mounted, we will need to umount/fsck on Teardown.
  state_ = FsTestState::kRunning;
  END_HELPER;
}

bool BlobfsTest::Teardown() {
  BEGIN_HELPER;
  ASSERT_NE(state_, FsTestState::kComplete);
  auto error = fbl::MakeAutoCall([this]() { state_ = FsTestState::kError; });

  if (state_ == FsTestState::kRunning) {
    ASSERT_EQ(state_, FsTestState::kRunning);
    ASSERT_TRUE(CheckInfo());
    zx_status_t status = umount(MOUNT_PATH);
    // Unmount will propagate the result of sync; for cases where the filesystem is disconnected
    // from the underlying device, ZX_ERR_IO_REFUSED is expected. Please see the newer version
    // of this test. i.e BlobfsTest::TearDown.
    ASSERT_TRUE(status == ZX_OK || status == ZX_ERR_IO_REFUSED, "Failed to unmount filesystem");
    ASSERT_EQ(fsck(device_path_, DISK_FORMAT_BLOBFS, &test_fsck_options, launch_stdio_sync), ZX_OK,
              "Filesystem fsck failed");
  }

  if (gUseRealDisk) {
    if (type_ == FsTestType::kFvm) {
      ASSERT_EQ(fvm_destroy(fvm_path_), ZX_OK);
    }
  } else {
    ASSERT_EQ(ramdisk_destroy(ramdisk_), ZX_OK);
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
    strlcpy(path, device_path_, len);
  }
  END_HELPER;
}

bool BlobfsTest::ForceReset() {
  BEGIN_HELPER;
  if (state_ == FsTestState::kComplete) {
    return Reset();
  }

  ASSERT_NE(state_, FsTestState::kInit);
  ASSERT_EQ(umount(MOUNT_PATH), ZX_OK, "Failed to unmount filesystem");

  if (gUseRealDisk) {
    if (type_ == FsTestType::kFvm) {
      ASSERT_EQ(fvm_destroy(fvm_path_), ZX_OK);
    }
  } else {
    ASSERT_EQ(ramdisk_destroy(ramdisk_), ZX_OK);
  }

  FsTestState old_state = state_;
  state_ = FsTestState::kInit;

  ASSERT_TRUE(Init(old_state));
  END_HELPER;
}

bool BlobfsTest::ToggleSleep(uint64_t blk_count) {
  BEGIN_HELPER;

  if (asleep_) {
    // If the ramdisk is asleep, wake it up.
    ASSERT_EQ(ramdisk_wake(ramdisk_), ZX_OK);
  } else {
    // If the ramdisk is active, put it to sleep after the specified block count.
    ASSERT_EQ(ramdisk_sleep_after(ramdisk_, blk_count), ZX_OK);
  }

  asleep_ = !asleep_;
  END_HELPER;
}

bool BlobfsTest::GetRamdiskCount(uint64_t* blk_count) const {
  BEGIN_HELPER;
  ramdisk_block_write_counts_t counts;

  ASSERT_EQ(ramdisk_get_block_counts(ramdisk_, &counts), ZX_OK);

  *blk_count = counts.received;
  END_HELPER;
}

bool BlobfsTest::CheckInfo(BlobfsUsage* usage) {
  fbl::unique_fd fd(open(MOUNT_PATH, O_RDONLY | O_DIRECTORY));
  ASSERT_TRUE(fd);

  zx_status_t status;
  fuchsia_io_FilesystemInfo info;
  fzl::FdioCaller caller(std::move(fd));
  ASSERT_EQ(fuchsia_io_DirectoryAdminQueryFilesystem(caller.borrow_channel(), &status, &info),
            ZX_OK);
  ASSERT_EQ(status, ZX_OK);
  const char* kFsName = "blobfs";
  const char* name = reinterpret_cast<const char*>(info.name);
  ASSERT_EQ(strncmp(name, kFsName, strlen(kFsName)), 0, "Unexpected filesystem mounted");
  ASSERT_LE(info.used_nodes, info.total_nodes, "Used nodes greater than free nodes");
  ASSERT_LE(info.used_bytes, info.total_bytes, "Used bytes greater than free bytes");
  ASSERT_EQ(close(caller.release().release()), 0);

  if (usage != nullptr) {
    usage->used_bytes = info.used_bytes;
    usage->total_bytes = info.total_bytes;
    usage->used_nodes = info.used_nodes;
    usage->total_nodes = info.total_nodes;
  }

  return true;
}

bool BlobfsTest::Mount() {
  BEGIN_HELPER;
  int flags = read_only_ ? O_RDONLY : O_RDWR;

  fbl::unique_fd fd(open(device_path_, flags));
  ASSERT_TRUE(fd.get(), "Could not open ramdisk");

  mount_options_t options = default_mount_options;
  options.enable_journal = gEnableJournal;
  options.enable_pager = gEnablePager;

  if (read_only_) {
    options.readonly = true;
  }

  LaunchCallback launch = stdio_ ? launch_stdio_async : launch_silent_async;

  // fd consumed by mount. By default, mount waits until the filesystem is
  // ready to accept commands.
  ASSERT_EQ(mount(fd.get(), MOUNT_PATH, DISK_FORMAT_BLOBFS, &options, launch), ZX_OK,
            "Could not mount blobfs");
  END_HELPER;
}

// Helper functions for testing:

static bool MakeBlobUnverified(fs_test_utils::BlobInfo* info, fbl::unique_fd* out_fd) {
  fbl::unique_fd fd(open(info->path, O_CREAT | O_RDWR));
  ASSERT_TRUE(fd, "Failed to create blob");
  ASSERT_EQ(ftruncate(fd.get(), info->size_data), 0);
  ASSERT_EQ(fs_test_utils::StreamAll(write, fd.get(), info->data.get(), info->size_data), 0,
            "Failed to write Data");
  out_fd->reset(fd.release());
  return true;
}

}  // namespace

// Actual tests:

static bool CreateUmountRemountLarge(BlobfsTest* blobfsTest) {
  BEGIN_HELPER;
  fs_test_utils::BlobList bl(MOUNT_PATH);
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
        ASSERT_TRUE(bl.CreateBlob(&seed));
        break;
      case 1:
        ASSERT_TRUE(bl.ConfigBlob());
        break;
      case 2:
        ASSERT_TRUE(bl.WriteData());
        break;
      case 3:
        ASSERT_TRUE(bl.ReadData());
        break;
      case 4:
        ASSERT_TRUE(bl.ReopenBlob());
        break;
      case 5:
        ASSERT_TRUE(bl.UnlinkBlob());
        break;
    }
  }

  // Close all currently opened nodes (REGARDLESS of their state)
  bl.CloseAll();

  // Unmount, remount
  ASSERT_TRUE(blobfsTest->Remount(), "Could not re-mount blobfs");

  // Reopen all (readable) blobs
  bl.OpenAll();

  // Verify state of all blobs
  bl.VerifyAll();

  // Close everything again
  bl.CloseAll();

  END_HELPER;
}

int unmount_remount_thread(void* arg) {
  fs_test_utils::BlobList* bl = static_cast<fs_test_utils::BlobList*>(arg);
  unsigned int seed = static_cast<unsigned int>(zx_ticks_get());
  unittest_printf("unmount_remount thread using seed: %u\n", seed);

  // Do some operations...
  size_t num_ops = 1000;
  for (size_t i = 0; i < num_ops; ++i) {
    switch (rand_r(&seed) % 6) {
      case 0:
        ASSERT_TRUE(bl->CreateBlob(&seed));
        break;
      case 1:
        ASSERT_TRUE(bl->ConfigBlob());
        break;
      case 2:
        ASSERT_TRUE(bl->WriteData());
        break;
      case 3:
        ASSERT_TRUE(bl->ReadData());
        break;
      case 4:
        ASSERT_TRUE(bl->ReopenBlob());
        break;
      case 5:
        ASSERT_TRUE(bl->UnlinkBlob());
        break;
    }
  }

  return 0;
}

static bool CreateUmountRemountLargeMultithreaded(BlobfsTest* blobfsTest) {
  BEGIN_HELPER;
  fs_test_utils::BlobList bl(MOUNT_PATH);

  size_t num_threads = 10;
  fbl::AllocChecker ac;
  fbl::Array<thrd_t> threads(new (&ac) thrd_t[num_threads](), num_threads);
  ASSERT_TRUE(ac.check());

  // Launch all threads
  for (size_t i = 0; i < num_threads; i++) {
    ASSERT_EQ(thrd_create(&threads[i], unmount_remount_thread, &bl), thrd_success);
  }

  // Wait for all threads to complete.
  // Currently, threads will always return a successful status.
  for (size_t i = 0; i < num_threads; i++) {
    int res;
    ASSERT_EQ(thrd_join(threads[i], &res), thrd_success);
    ASSERT_EQ(res, 0);
  }

  // Close all currently opened nodes (REGARDLESS of their state)
  bl.CloseAll();

  // Unmount, remount
  ASSERT_TRUE(blobfsTest->Remount(), "Could not re-mount blobfs");

  // reopen all blobs
  bl.OpenAll();

  // verify all blob contents
  bl.VerifyAll();

  // close everything again
  bl.CloseAll();

  END_HELPER;
}

typedef struct reopen_data {
  char path[PATH_MAX];
  std::atomic_bool complete;
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
static bool CreateWriteReopen(BlobfsTest* blobfsTest) {
  BEGIN_HELPER;
  size_t num_ops = 10;

  std::unique_ptr<fs_test_utils::BlobInfo> anchor_info;
  ASSERT_TRUE(fs_test_utils::GenerateRandomBlob(MOUNT_PATH, 1 << 10, &anchor_info));

  std::unique_ptr<fs_test_utils::BlobInfo> info;
  ASSERT_TRUE(fs_test_utils::GenerateRandomBlob(MOUNT_PATH, 10 * (1 << 20), &info));
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

    int result;
    int success;
    thrd_t thread;
    ASSERT_EQ(thrd_create(&thread, reopen_thread, &dat), thrd_success);

    {
      // In case the test fails, always join the thread before returning from the test.
      auto join_thread = fbl::MakeAutoCall([&]() {
        atomic_store(&dat.complete, true);
        success = thrd_join(thread, &result);
      });

      // Sleep while the thread continually opens and closes the blob
      usleep(1000000);
      ASSERT_EQ(syncfs(anchor_fd.get()), 0);
    }

    ASSERT_EQ(success, thrd_success);
    ASSERT_EQ(result, 0);

    ASSERT_EQ(close(anchor_fd.release()), 0);
    ASSERT_EQ(unlink(info->path), 0);
    ASSERT_EQ(unlink(anchor_info->path), 0);
  }

  END_HELPER;
}

// TODO(ZX-2416): Add tests to manually corrupt journal entries/metadata.

BEGIN_TEST_CASE(blobfs_tests)
RUN_TESTS(LARGE, CreateUmountRemountLarge)
RUN_TESTS(LARGE, CreateUmountRemountLargeMultithreaded)
RUN_TESTS(LARGE, CreateWriteReopen)
END_TEST_CASE(blobfs_tests)

// TODO(planders): revamp blobfs test options.
static void print_test_help(FILE* f) {
  fprintf(f,
          "  -d <blkdev>\n"
          "      Use block device <blkdev> instead of a ramdisk\n"
          "  -f <count>\n"
          "      For each test, run the test <count> additional times,\n"
          "        intentionally causing the underlying device driver to\n"
          "        'sleep' after a certain number of block writes.\n"
          "      After each additional test, the blobfs partition will be\n"
          "        remounted and checked for consistency via fsck.\n"
          "      If <count> is 0, the maximum number of tests are run.\n"
          "      This option is only valid when using a ramdisk.\n"
          "  -j\n"
          "      Disable the journal\n"
          "  -p\n"
          "      Enable the pager\n"
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
      }
      fzl::FdioCaller caller(std::move(fd));
      size_t path_len;
      auto resp = ::llcpp::fuchsia::device::Controller::Call::GetTopologicalPath(
          zx::unowned_channel(caller.borrow_channel()));
      zx_status_t status = resp.status();
      if (status == ZX_OK) {
        if (resp->result.is_err()) {
          status = resp->result.err();
        } else {
          auto r = resp->result.response();
          path_len = r.path.size();
          if (path_len > PATH_MAX) {
            return ZX_ERR_INTERNAL;
          }
          memcpy(gRealDiskInfo.disk_path, r.path.data(), r.path.size());
        }
      }

      if (status != ZX_OK) {
        fprintf(stderr, "[fs] Could not acquire topological path of block device\n");
        return -1;
      }
      gRealDiskInfo.disk_path[path_len] = 0;

      // If we previously tried running tests on this disk, it may
      // have created an FVM and failed. (Try to) clean up from previous state
      // before re-running.
      fvm_destroy(gRealDiskInfo.disk_path);

      fuchsia_hardware_block_BlockInfo block_info;
      zx_status_t io_status =
          fuchsia_hardware_block_BlockGetInfo(caller.borrow_channel(), &status, &block_info);
      if (io_status != ZX_OK) {
        status = io_status;
      }

      if (status != ZX_OK) {
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
    } else if (!strcmp(argv[i], "-f") && (i + 1 < argc)) {
      gEnableRamdiskFailure = true;
      gRamdiskFailureLoops = atoi(argv[i + 1]);
      i += 2;
    } else if (!strcmp(argv[i], "-j")) {
      gEnableJournal = false;
      i++;
    } else if (!strcmp(argv[i], "-p")) {
      gEnablePager = true;
      i++;
    } else {
      // Ignore options we don't recognize. See ulib/unittest/README.md.
      break;
    }
  }

  if (gUseRealDisk && gEnableRamdiskFailure) {
    fprintf(stderr, "Error: Ramdisk failure not allowed for real disk\n");
    return -1;
  }

  // Initialize tmpfs.
  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
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
