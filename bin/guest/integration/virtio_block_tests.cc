// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fbl/unique_fd.h>
#include <fcntl.h>
#include <gmock/gmock.h>
#include <lib/fdio/util.h>
#include <lib/fxl/arraysize.h>
#include <lib/fxl/strings/string_printf.h>
#include <limits.h>
#include <stdlib.h>
#include <string>

#include "garnet/bin/guest/vmm/device/block.h"
#include "garnet/bin/guest/vmm/device/qcow.h"
#include "garnet/bin/guest/vmm/device/qcow_test_data.h"

#include "guest_test.h"

using namespace qcow_test_data;
using testing::HasSubstr;

static constexpr char kVirtioBlockUtilCmx[] = "meta/virtio_block_test_util.cmx";
static constexpr uint32_t kVirtioBlockCount = 32;
static constexpr uint32_t kVirtioQcowBlockCount = 4 * 1024 * 1024 * 2;
static constexpr uint32_t kVirtioTestStep = 8;

static fidl::VectorPtr<fuchsia::guest::BlockDevice> block_device(
    fuchsia::guest::BlockMode mode, fuchsia::guest::BlockFormat format,
    char* path) {
  fbl::unique_fd fd(mkstemp(path));
  FXL_CHECK(fd) << "Failed to create temporary file";

  zx_handle_t handle;
  zx_status_t status = fdio_get_service_handle(fd.release(), &handle);
  FXL_CHECK(status == ZX_OK) << "Failed to get temporary file handle";

  fidl::VectorPtr<fuchsia::guest::BlockDevice> block_devices;
  block_devices.push_back({
      "test_device",
      mode,
      format,
      fidl::InterfaceHandle<fuchsia::io::File>(zx::channel(handle)).Bind(),
  });
  return block_devices;
}

static bool write_raw_file(const char* path) {
  fbl::unique_fd fd(open(path, O_RDWR));
  if (!fd) {
    return false;
  }
  int ret = ftruncate(fd.get(), kVirtioBlockCount * kBlockSectorSize);
  return ret == 0;
}

class ZirconReadOnlyRawGuestTest
    : public GuestTest<ZirconReadOnlyRawGuestTest> {
 public:
  static bool LaunchInfo(fuchsia::guest::LaunchInfo* launch_info) {
    launch_info->url = kZirconGuestUrl;
    launch_info->args.push_back("--virtio-gpu=false");
    launch_info->args.push_back("--cmdline-add=kernel.serial=none");
    launch_info->block_devices =
        block_device(fuchsia::guest::BlockMode::READ_ONLY,
                     fuchsia::guest::BlockFormat::RAW, file_path_);
    return write_raw_file(file_path_);
  }

  static bool SetUpGuest() {
    if (WaitForAppmgrReady() != ZX_OK) {
      ADD_FAILURE() << "Failed to wait for appmgr ready";
      return false;
    }
    return true;
  }

  static char file_path_[PATH_MAX];
};

char ZirconReadOnlyRawGuestTest::file_path_[PATH_MAX] =
    "/tmp/guest-test.XXXXXX";

TEST_F(ZirconReadOnlyRawGuestTest, BlockDeviceExists) {
  std::string args =
      fxl::StringPrintf("%lu %u check", kBlockSectorSize, kVirtioBlockCount);
  std::string result;
  EXPECT_EQ(Run(kVirtioBlockUtilCmx, args, &result), ZX_OK);
  EXPECT_THAT(result, HasSubstr("PASS"));
}

TEST_F(ZirconReadOnlyRawGuestTest, Read) {
  fbl::unique_fd fd(open(file_path_, O_RDWR));
  ASSERT_TRUE(fd);

  uint8_t data[kBlockSectorSize];
  memset(data, 0xab, kBlockSectorSize);
  for (off_t offset = 0; offset != kVirtioBlockCount;
       offset += kVirtioTestStep) {
    ASSERT_EQ(
        pwrite(fd.get(), &data, kBlockSectorSize, offset * kBlockSectorSize),
        static_cast<ssize_t>(kBlockSectorSize));
    std::string args =
        fxl::StringPrintf("%lu %u read %d %d", kBlockSectorSize,
                          kVirtioBlockCount, static_cast<int>(offset), 0xab);
    std::string result;
    EXPECT_EQ(Run(kVirtioBlockUtilCmx, args, &result), ZX_OK);
    EXPECT_THAT(result, HasSubstr("PASS"));
  }
}

TEST_F(ZirconReadOnlyRawGuestTest, Write) {
  fbl::unique_fd fd(open(file_path_, O_RDWR));
  ASSERT_TRUE(fd);

  uint8_t data[kBlockSectorSize];
  memset(data, 0, kBlockSectorSize);
  for (off_t offset = 0; offset != kVirtioBlockCount;
       offset += kVirtioTestStep) {
    // Write the block to zero.
    ASSERT_EQ(
        pwrite(fd.get(), &data, kBlockSectorSize, offset * kBlockSectorSize),
        static_cast<ssize_t>(kBlockSectorSize));

    // Tell the guest to write bytes to the block.
    std::string args =
        fxl::StringPrintf("%lu %u write %d %d", kBlockSectorSize,
                          kVirtioBlockCount, static_cast<int>(offset), 0xab);
    std::string result;
    EXPECT_EQ(Run(kVirtioBlockUtilCmx, args, &result), ZX_OK);
    EXPECT_THAT(result, HasSubstr("PASS"));

    // Check that the guest reads zero from the block (i.e. it wasn't written).
    args = fxl::StringPrintf("%lu %u read %d %d", kBlockSectorSize,
                             kVirtioBlockCount, static_cast<int>(offset), 0);
    EXPECT_EQ(Run(kVirtioBlockUtilCmx, args, &result), ZX_OK);
    EXPECT_THAT(result, HasSubstr("PASS"));

    // Check that the host block contains only zero.
    ASSERT_EQ(
        pread(fd.get(), &data, kBlockSectorSize, offset * kBlockSectorSize),
        static_cast<ssize_t>(kBlockSectorSize));
    for (off_t i = 0; i != kBlockSectorSize; ++i) {
      EXPECT_EQ(data[i], 0);
    }
  }
}

class ZirconReadWriteRawGuestTest
    : public GuestTest<ZirconReadWriteRawGuestTest> {
 public:
  static bool LaunchInfo(fuchsia::guest::LaunchInfo* launch_info) {
    launch_info->url = kZirconGuestUrl;
    launch_info->args.push_back("--virtio-gpu=false");
    launch_info->args.push_back("--cmdline-add=kernel.serial=none");
    launch_info->block_devices =
        block_device(fuchsia::guest::BlockMode::READ_WRITE,
                     fuchsia::guest::BlockFormat::RAW, file_path_);
    return write_raw_file(file_path_);
  }

  static bool SetUpGuest() {
    if (WaitForAppmgrReady() != ZX_OK) {
      ADD_FAILURE() << "Failed to wait for appmgr ready";
      return false;
    }
    return true;
  }

  static char file_path_[PATH_MAX];
};

char ZirconReadWriteRawGuestTest::file_path_[PATH_MAX] =
    "/tmp/guest-test.XXXXXX";

TEST_F(ZirconReadWriteRawGuestTest, BlockDeviceExists) {
  std::string args =
      fxl::StringPrintf("%lu %u check", kBlockSectorSize, kVirtioBlockCount);
  std::string result;
  EXPECT_EQ(Run(kVirtioBlockUtilCmx, args, &result), ZX_OK);
  EXPECT_THAT(result, HasSubstr("PASS"));
}

TEST_F(ZirconReadWriteRawGuestTest, Read) {
  fbl::unique_fd fd(open(file_path_, O_RDWR));
  ASSERT_TRUE(fd);

  uint8_t data[kBlockSectorSize];
  memset(data, 0xab, kBlockSectorSize);
  for (off_t offset = 0; offset != kVirtioBlockCount;
       offset += kVirtioTestStep) {
    ASSERT_EQ(
        pwrite(fd.get(), &data, kBlockSectorSize, offset * kBlockSectorSize),
        static_cast<ssize_t>(kBlockSectorSize));
    std::string args =
        fxl::StringPrintf("%lu %u read %d %d", kBlockSectorSize,
                          kVirtioBlockCount, static_cast<int>(offset), 0xab);
    std::string result;
    EXPECT_EQ(Run(kVirtioBlockUtilCmx, args, &result), ZX_OK);
    EXPECT_THAT(result, HasSubstr("PASS"));
  }
}

TEST_F(ZirconReadWriteRawGuestTest, Write) {
  fbl::unique_fd fd(open(file_path_, O_RDWR));
  ASSERT_TRUE(fd);

  uint8_t data[kBlockSectorSize];
  memset(data, 0, kBlockSectorSize);
  for (off_t offset = 0; offset != kVirtioBlockCount;
       offset += kVirtioTestStep) {
    // Write the block to zero.
    ASSERT_EQ(
        pwrite(fd.get(), &data, kBlockSectorSize, offset * kBlockSectorSize),
        static_cast<ssize_t>(kBlockSectorSize));

    // Tell the guest to write bytes to the block.
    std::string args =
        fxl::StringPrintf("%lu %u write %d %d", kBlockSectorSize,
                          kVirtioBlockCount, static_cast<int>(offset), 0xab);
    std::string result;
    EXPECT_EQ(Run(kVirtioBlockUtilCmx, args, &result), ZX_OK);
    EXPECT_THAT(result, HasSubstr("PASS"));

    // Check that the guest reads the written bytes from the block.
    args = fxl::StringPrintf("%lu %u read %d %d", kBlockSectorSize,
                             kVirtioBlockCount, static_cast<int>(offset), 0xab);
    EXPECT_EQ(Run(kVirtioBlockUtilCmx, args, &result), ZX_OK);
    EXPECT_THAT(result, HasSubstr("PASS"));

    // Check that the host block contains the written bytes.
    ASSERT_EQ(
        pread(fd.get(), &data, kBlockSectorSize, offset * kBlockSectorSize),
        static_cast<ssize_t>(kBlockSectorSize));
    for (off_t i = 0; i != kBlockSectorSize; ++i) {
      EXPECT_EQ(data[i], 0xab);
    }
  }
}

class ZirconVolatileRawGuestTest
    : public GuestTest<ZirconVolatileRawGuestTest> {
 public:
  static bool LaunchInfo(fuchsia::guest::LaunchInfo* launch_info) {
    launch_info->url = kZirconGuestUrl;
    launch_info->args.push_back("--virtio-gpu=false");
    launch_info->args.push_back("--cmdline-add=kernel.serial=none");
    launch_info->block_devices =
        block_device(fuchsia::guest::BlockMode::VOLATILE_WRITE,
                     fuchsia::guest::BlockFormat::RAW, file_path_);
    return write_raw_file(file_path_);
  }

  static bool SetUpGuest() {
    if (WaitForAppmgrReady() != ZX_OK) {
      ADD_FAILURE() << "Failed to wait for appmgr ready";
      return false;
    }
    return true;
  }

  static char file_path_[PATH_MAX];
};

char ZirconVolatileRawGuestTest::file_path_[PATH_MAX] =
    "/tmp/guest-test.XXXXXX";

TEST_F(ZirconVolatileRawGuestTest, BlockDeviceExists) {
  std::string args =
      fxl::StringPrintf("%lu %u check", kBlockSectorSize, kVirtioBlockCount);
  std::string result;
  EXPECT_EQ(Run(kVirtioBlockUtilCmx, args, &result), ZX_OK);
  EXPECT_THAT(result, HasSubstr("PASS"));
}

TEST_F(ZirconVolatileRawGuestTest, Read) {
  fbl::unique_fd fd(open(file_path_, O_RDWR));
  ASSERT_TRUE(fd);

  uint8_t data[kBlockSectorSize];
  memset(data, 0xab, kBlockSectorSize);
  for (off_t offset = 0; offset != kVirtioBlockCount;
       offset += kVirtioTestStep) {
    ASSERT_EQ(
        pwrite(fd.get(), &data, kBlockSectorSize, offset * kBlockSectorSize),
        static_cast<ssize_t>(kBlockSectorSize));
    std::string args =
        fxl::StringPrintf("%lu %u read %d %d", kBlockSectorSize,
                          kVirtioBlockCount, static_cast<int>(offset), 0xab);
    std::string result;
    EXPECT_EQ(Run(kVirtioBlockUtilCmx, args, &result), ZX_OK);
    EXPECT_THAT(result, HasSubstr("PASS"));
  }
}

TEST_F(ZirconVolatileRawGuestTest, Write) {
  fbl::unique_fd fd(open(file_path_, O_RDWR));
  ASSERT_TRUE(fd);

  uint8_t data[kBlockSectorSize];
  memset(data, 0, kBlockSectorSize);
  for (off_t offset = 0; offset != kVirtioBlockCount;
       offset += kVirtioTestStep) {
    // Write the block to zero.
    ASSERT_EQ(
        pwrite(fd.get(), &data, kBlockSectorSize, offset * kBlockSectorSize),
        static_cast<ssize_t>(kBlockSectorSize));

    // Tell the guest to write bytes to the block.
    std::string args =
        fxl::StringPrintf("%lu %u write %d %d", kBlockSectorSize,
                          kVirtioBlockCount, static_cast<int>(offset), 0xab);
    std::string result;
    EXPECT_EQ(Run(kVirtioBlockUtilCmx, args, &result), ZX_OK);
    EXPECT_THAT(result, HasSubstr("PASS"));

    // Check that the guest reads the written bytes from the block.
    args = fxl::StringPrintf("%lu %u read %d %d", kBlockSectorSize,
                             kVirtioBlockCount, static_cast<int>(offset), 0xab);
    EXPECT_EQ(Run(kVirtioBlockUtilCmx, args, &result), ZX_OK);
    EXPECT_THAT(result, HasSubstr("PASS"));

    // Check that the host block contains only zero (i.e. was not written).
    ASSERT_EQ(
        pread(fd.get(), &data, kBlockSectorSize, offset * kBlockSectorSize),
        static_cast<ssize_t>(kBlockSectorSize));
    for (off_t i = 0; i != kBlockSectorSize; ++i) {
      EXPECT_EQ(data[i], 0);
    }
  }
}

template <typename T>
static bool write_at(int fd, const T* ptr, off_t off) {
  ssize_t written = pwrite(fd, ptr, sizeof(T), off);
  return written == static_cast<ssize_t>(sizeof(T));
}

// Writes an array of T values at the current file location.
template <typename T>
static bool write_at(int fd, const T* ptr, size_t len, off_t off) {
  ssize_t written = pwrite(fd, ptr, len * sizeof(T), off);
  return written == static_cast<ssize_t>(len * sizeof(T));
}

static bool write_qcow_file(const char* path) {
  fbl::unique_fd fd(open(path, O_RDWR));
  if (!fd) {
    return false;
  }

  QcowHeader header = kDefaultHeaderV2.HostToBigEndian();
  bool write_success = write_at(fd.get(), &header, 0);
  if (!write_success) {
    return false;
  }

  // Convert L1 entries to big-endian
  uint64_t be_table[arraysize(kL2TableClusterOffsets)];
  for (size_t i = 0; i < arraysize(kL2TableClusterOffsets); ++i) {
    be_table[i] = HostToBigEndianTraits::Convert(kL2TableClusterOffsets[i]);
  }

  // Write L1 table.
  write_success =
      write_at(fd.get(), be_table, arraysize(kL2TableClusterOffsets),
               kDefaultHeaderV2.l1_table_offset);
  if (!write_success) {
    return false;
  }

  // Initialize empty L2 tables.
  for (size_t i = 0; i < arraysize(kL2TableClusterOffsets); ++i) {
    write_success = write_at(fd.get(), kZeroCluster, sizeof(kZeroCluster),
                             kL2TableClusterOffsets[i]);
    if (!write_success) {
      return false;
    }
  }

  // Write L2 entry
  uint64_t l2_offset = kL2TableClusterOffsets[0];
  uint64_t data_cluster_offset = ClusterOffset(kFirstDataCluster);
  uint64_t l2_entry = HostToBigEndianTraits::Convert(data_cluster_offset);
  write_success = write_at(fd.get(), &l2_entry, l2_offset);
  if (!write_success) {
    return false;
  }

  // Write data to cluster.
  uint8_t cluster_data[kClusterSize];
  memset(cluster_data, 0xab, sizeof(cluster_data));
  write_success =
      write_at(fd.get(), cluster_data, kClusterSize, data_cluster_offset);
  if (!write_success) {
    return false;
  }

  return true;
}

class ZirconReadOnlyQcowGuestTest
    : public GuestTest<ZirconReadOnlyQcowGuestTest> {
 public:
  static bool LaunchInfo(fuchsia::guest::LaunchInfo* launch_info) {
    launch_info->url = kZirconGuestUrl;
    launch_info->args.push_back("--virtio-gpu=false");
    launch_info->args.push_back("--cmdline-add=kernel.serial=none");
    launch_info->block_devices =
        block_device(fuchsia::guest::BlockMode::READ_ONLY,
                     fuchsia::guest::BlockFormat::QCOW, file_path_);
    return write_qcow_file(file_path_);
  }

  static bool SetUpGuest() {
    if (WaitForAppmgrReady() != ZX_OK) {
      ADD_FAILURE() << "Failed to wait for appmgr ready";
      return false;
    }
    return true;
  }

  static char file_path_[PATH_MAX];
};

char ZirconReadOnlyQcowGuestTest::file_path_[PATH_MAX] =
    "/tmp/guest-test.XXXXXX";

TEST_F(ZirconReadOnlyQcowGuestTest, BlockDeviceExists) {
  std::string args = fxl::StringPrintf("%lu %u check", kBlockSectorSize,
                                       kVirtioQcowBlockCount);
  std::string result;
  EXPECT_EQ(Run(kVirtioBlockUtilCmx, args, &result), ZX_OK);
  EXPECT_THAT(result, HasSubstr("PASS"));
}

TEST_F(ZirconReadOnlyQcowGuestTest, ReadMappedCluster) {
  for (off_t offset = 0; offset != kClusterSize / kBlockSectorSize;
       offset += kVirtioTestStep) {
    std::string args =
        fxl::StringPrintf("%lu %u read %d %d",

                          kBlockSectorSize, kVirtioQcowBlockCount,
                          static_cast<int>(offset), 0xab);
    std::string result;
    EXPECT_EQ(Run(kVirtioBlockUtilCmx, args, &result), ZX_OK);
    EXPECT_THAT(result, HasSubstr("PASS"));
  }
}

TEST_F(ZirconReadOnlyQcowGuestTest, ReadUnmappedCluster) {
  for (off_t offset = kClusterSize;
       offset != kClusterSize + (kClusterSize / kBlockSectorSize);
       offset += kVirtioTestStep) {
    std::string args =
        fxl::StringPrintf("%lu %u read %d %d", kBlockSectorSize,
                          kVirtioQcowBlockCount, static_cast<int>(offset), 0);
    std::string result;
    EXPECT_EQ(Run(kVirtioBlockUtilCmx, args, &result), ZX_OK);
    EXPECT_THAT(result, HasSubstr("PASS"));
  }
}

TEST_F(ZirconReadOnlyQcowGuestTest, Write) {
  for (off_t offset = kClusterSize;
       offset != kClusterSize + (kClusterSize / kBlockSectorSize);
       offset += kVirtioTestStep) {
    std::string args =
        fxl::StringPrintf("%lu %u write %d %d",

                          kBlockSectorSize, kVirtioQcowBlockCount,
                          static_cast<int>(offset), 0xab);
    std::string result;
    EXPECT_EQ(Run(kVirtioBlockUtilCmx, args, &result), ZX_OK);
    EXPECT_THAT(result, HasSubstr("PASS"));

    args =
        fxl::StringPrintf("%lu %u read %d %d", kBlockSectorSize,
                          kVirtioQcowBlockCount, static_cast<int>(offset), 0);
    EXPECT_EQ(Run(kVirtioBlockUtilCmx, args, &result), ZX_OK);
    EXPECT_THAT(result, HasSubstr("PASS"));
  }
}

class ZirconVolatileQcowGuestTest
    : public GuestTest<ZirconVolatileQcowGuestTest> {
 public:
  static bool LaunchInfo(fuchsia::guest::LaunchInfo* launch_info) {
    launch_info->url = kZirconGuestUrl;
    launch_info->args.push_back("--virtio-gpu=false");
    launch_info->args.push_back("--cmdline-add=kernel.serial=none");
    launch_info->block_devices =
        block_device(fuchsia::guest::BlockMode::VOLATILE_WRITE,
                     fuchsia::guest::BlockFormat::QCOW, file_path_);
    return write_qcow_file(file_path_);
  }

  static bool SetUpGuest() {
    if (WaitForAppmgrReady() != ZX_OK) {
      ADD_FAILURE() << "Failed to wait for appmgr ready";
      return false;
    }
    return true;
  }

  static char file_path_[PATH_MAX];
};

char ZirconVolatileQcowGuestTest::file_path_[PATH_MAX] =
    "/tmp/guest-test.XXXXXX";

TEST_F(ZirconVolatileQcowGuestTest, BlockDeviceExists) {
  std::string args = fxl::StringPrintf("%lu %u check", kBlockSectorSize,
                                       kVirtioQcowBlockCount);
  std::string result;
  EXPECT_EQ(Run(kVirtioBlockUtilCmx, args, &result), ZX_OK);
  EXPECT_THAT(result, HasSubstr("PASS"));
}

TEST_F(ZirconVolatileQcowGuestTest, ReadMappedCluster) {
  for (off_t offset = 0; offset != kClusterSize / kBlockSectorSize;
       offset += kVirtioTestStep) {
    std::string args =
        fxl::StringPrintf("%lu %u read %d %d",

                          kBlockSectorSize, kVirtioQcowBlockCount,
                          static_cast<int>(offset), 0xab);
    std::string result;
    EXPECT_EQ(Run(kVirtioBlockUtilCmx, args, &result), ZX_OK);
    EXPECT_THAT(result, HasSubstr("PASS"));
  }
}

TEST_F(ZirconVolatileQcowGuestTest, ReadUnmappedCluster) {
  for (off_t offset = kClusterSize;
       offset != kClusterSize + (kClusterSize / kBlockSectorSize);
       offset += kVirtioTestStep) {
    std::string args =
        fxl::StringPrintf("%lu %u read %d %d", kBlockSectorSize,
                          kVirtioQcowBlockCount, static_cast<int>(offset), 0);
    std::string result;
    EXPECT_EQ(Run(kVirtioBlockUtilCmx, args, &result), ZX_OK);
    EXPECT_THAT(result, HasSubstr("PASS"));
  }
}

TEST_F(ZirconVolatileQcowGuestTest, Write) {
  for (off_t offset = kClusterSize;
       offset != kClusterSize + (kClusterSize / kBlockSectorSize);
       offset += kVirtioTestStep) {
    std::string args =
        fxl::StringPrintf("%lu %u write %d %d",

                          kBlockSectorSize, kVirtioQcowBlockCount,
                          static_cast<int>(offset), 0xab);
    std::string result;
    EXPECT_EQ(Run(kVirtioBlockUtilCmx, args, &result), ZX_OK);
    EXPECT_THAT(result, HasSubstr("PASS"));

    args = fxl::StringPrintf("%lu %u read %d %d", kBlockSectorSize,
                             kVirtioQcowBlockCount, static_cast<int>(offset),
                             0xab);
    EXPECT_EQ(Run(kVirtioBlockUtilCmx, args, &result), ZX_OK);
    EXPECT_THAT(result, HasSubstr("PASS"));
  }
}
