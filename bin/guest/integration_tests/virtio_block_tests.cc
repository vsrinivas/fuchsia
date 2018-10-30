// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <limits.h>
#include <stdlib.h>
#include <string>

#include <fbl/unique_fd.h>
#include <fs-management/ramdisk.h>
#include <garnet/bin/guest/vmm/device/qcow.h>
#include <garnet/bin/guest/vmm/device/qcow_test_data.h>
#include <garnet/lib/machina/device/block.h>
#include <gmock/gmock.h>
#include <lib/fxl/strings/string_printf.h>

#include "guest_test.h"

using namespace qcow_test_data;
using ::machina::kBlockSectorSize;
using ::testing::HasSubstr;

static constexpr char kTestUtilsUrl[] =
    "fuchsia-pkg://fuchsia.com/guest_integration_tests_utils";
static constexpr char kVirtioBlockUtilCmx[] = "meta/virtio_block_test_util.cmx";

static constexpr uint32_t kVirtioBlockCount = 32;
static constexpr uint32_t kVirtioQcowBlockCount = 4 * 1024 * 1024 * 2;
static constexpr uint32_t kVirtioTestStep = 8;

class ZirconReadOnlyRamdiskGuestTest
    : public GuestTest<ZirconReadOnlyRamdiskGuestTest> {
 public:
  static bool LaunchInfo(fuchsia::guest::LaunchInfo* launch_info) {
    create_ramdisk(kBlockSectorSize, kVirtioBlockCount, ramdisk_path_);
    launch_info->url = kZirconGuestUrl;
    launch_info->args.push_back("--display=none");
    launch_info->args.push_back("--cpus=1");
    launch_info->args.push_back(
        fxl::StringPrintf("--block=%s,ro", ramdisk_path_));
    return true;
  }

  static bool SetUpGuest() {
    if (WaitForPkgfs() != ZX_OK) {
      ADD_FAILURE() << "Failed to wait for pkgfs";
      return false;
    }
    return true;
  }

  static char ramdisk_path_[PATH_MAX];
};

char ZirconReadOnlyRamdiskGuestTest::ramdisk_path_[PATH_MAX] = "";

TEST_F(ZirconReadOnlyRamdiskGuestTest, BlockDeviceExists) {
  ASSERT_EQ(WaitForPkgfs(), ZX_OK);
  std::string cmd = fxl::StringPrintf("run %s#%s %lu %u check", kTestUtilsUrl,
                                      kVirtioBlockUtilCmx, kBlockSectorSize,
                                      kVirtioBlockCount);
  std::string result;
  EXPECT_EQ(Execute(cmd, &result), ZX_OK);
  EXPECT_THAT(result, HasSubstr("PASS"));
}

TEST_F(ZirconReadOnlyRamdiskGuestTest, Read) {
  ASSERT_EQ(WaitForPkgfs(), ZX_OK);
  fbl::unique_fd fd(open(ramdisk_path_, O_RDWR));
  ASSERT_TRUE(fd);

  uint8_t data[kBlockSectorSize];
  memset(data, 0xab, kBlockSectorSize);
  for (off_t offset = 0; offset != kVirtioBlockCount;
       offset += kVirtioTestStep) {
    ASSERT_EQ(
        pwrite(fd.get(), &data, kBlockSectorSize, offset * kBlockSectorSize),
        static_cast<ssize_t>(kBlockSectorSize));
    std::string cmd = fxl::StringPrintf(
        "run %s#%s %lu %u read %d %d", kTestUtilsUrl, kVirtioBlockUtilCmx,
        kBlockSectorSize, kVirtioBlockCount, static_cast<int>(offset), 0xab);
    std::string result;
    EXPECT_EQ(Execute(cmd, &result), ZX_OK);
    EXPECT_THAT(result, HasSubstr("PASS"));
  }
}

TEST_F(ZirconReadOnlyRamdiskGuestTest, Write) {
  ASSERT_EQ(WaitForPkgfs(), ZX_OK);
  fbl::unique_fd fd(open(ramdisk_path_, O_RDWR));
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
    std::string cmd = fxl::StringPrintf(
        "run %s#%s %lu %u write %d %d", kTestUtilsUrl, kVirtioBlockUtilCmx,
        kBlockSectorSize, kVirtioBlockCount, static_cast<int>(offset), 0xab);
    std::string result;
    EXPECT_EQ(Execute(cmd, &result), ZX_OK);
    EXPECT_THAT(result, HasSubstr("PASS"));

    // Check that the guest reads zero from the block (i.e. it wasn't written).
    cmd = fxl::StringPrintf("run %s#%s %lu %u read %d %d", kTestUtilsUrl,
                            kVirtioBlockUtilCmx, kBlockSectorSize,
                            kVirtioBlockCount, static_cast<int>(offset), 0);
    EXPECT_EQ(Execute(cmd, &result), ZX_OK);
    EXPECT_THAT(result, HasSubstr("PASS"));

    // Check that the ramdisk block contains only zero.
    ASSERT_EQ(
        pread(fd.get(), &data, kBlockSectorSize, offset * kBlockSectorSize),
        static_cast<ssize_t>(kBlockSectorSize));
    for (off_t i = 0; i != kBlockSectorSize; ++i) {
      EXPECT_EQ(data[i], 0);
    }
  }
}

class ZirconReadWriteRamdiskGuestTest
    : public GuestTest<ZirconReadWriteRamdiskGuestTest> {
 public:
  static bool LaunchInfo(fuchsia::guest::LaunchInfo* launch_info) {
    create_ramdisk(kBlockSectorSize, kVirtioBlockCount, ramdisk_path_);
    launch_info->url = kZirconGuestUrl;
    launch_info->args.push_back("--display=none");
    launch_info->args.push_back("--cpus=1");
    launch_info->args.push_back(
        fxl::StringPrintf("--block=%s,rw", ramdisk_path_));
    return true;
  }

  static bool SetUpGuest() {
    if (WaitForPkgfs() != ZX_OK) {
      ADD_FAILURE() << "Failed to wait for pkgfs";
      return false;
    }
    return true;
  }

  static char ramdisk_path_[PATH_MAX];
};

char ZirconReadWriteRamdiskGuestTest::ramdisk_path_[PATH_MAX] = "";

TEST_F(ZirconReadWriteRamdiskGuestTest, BlockDeviceExists) {
  ASSERT_EQ(WaitForPkgfs(), ZX_OK);
  std::string cmd = fxl::StringPrintf("run %s#%s %lu %u check", kTestUtilsUrl,
                                      kVirtioBlockUtilCmx, kBlockSectorSize,
                                      kVirtioBlockCount);
  std::string result;
  EXPECT_EQ(Execute(cmd, &result), ZX_OK);
  EXPECT_THAT(result, HasSubstr("PASS"));
}

TEST_F(ZirconReadWriteRamdiskGuestTest, Read) {
  ASSERT_EQ(WaitForPkgfs(), ZX_OK);
  fbl::unique_fd fd(open(ramdisk_path_, O_RDWR));
  ASSERT_TRUE(fd);

  uint8_t data[kBlockSectorSize];
  memset(data, 0xab, kBlockSectorSize);
  for (off_t offset = 0; offset != kVirtioBlockCount;
       offset += kVirtioTestStep) {
    ASSERT_EQ(
        pwrite(fd.get(), &data, kBlockSectorSize, offset * kBlockSectorSize),
        static_cast<ssize_t>(kBlockSectorSize));
    std::string cmd = fxl::StringPrintf(
        "run %s#%s %lu %u read %d %d", kTestUtilsUrl, kVirtioBlockUtilCmx,
        kBlockSectorSize, kVirtioBlockCount, static_cast<int>(offset), 0xab);
    std::string result;
    EXPECT_EQ(Execute(cmd, &result), ZX_OK);
    EXPECT_THAT(result, HasSubstr("PASS"));
  }
}

TEST_F(ZirconReadWriteRamdiskGuestTest, Write) {
  ASSERT_EQ(WaitForPkgfs(), ZX_OK);
  fbl::unique_fd fd(open(ramdisk_path_, O_RDWR));
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
    std::string cmd = fxl::StringPrintf(
        "run %s#%s %lu %u write %d %d", kTestUtilsUrl, kVirtioBlockUtilCmx,
        kBlockSectorSize, kVirtioBlockCount, static_cast<int>(offset), 0xab);
    std::string result;
    EXPECT_EQ(Execute(cmd, &result), ZX_OK);
    EXPECT_THAT(result, HasSubstr("PASS"));

    // Check that the guest reads the written bytes from the block.
    cmd = fxl::StringPrintf("run %s#%s %lu %u read %d %d", kTestUtilsUrl,
                            kVirtioBlockUtilCmx, kBlockSectorSize,
                            kVirtioBlockCount, static_cast<int>(offset), 0xab);
    EXPECT_EQ(Execute(cmd, &result), ZX_OK);
    EXPECT_THAT(result, HasSubstr("PASS"));

    // Check that the ramdisk block contains the written bytes.
    ASSERT_EQ(
        pread(fd.get(), &data, kBlockSectorSize, offset * kBlockSectorSize),
        static_cast<ssize_t>(kBlockSectorSize));
    for (off_t i = 0; i != kBlockSectorSize; ++i) {
      EXPECT_EQ(data[i], 0xab);
    }
  }
}

class ZirconVolatileRamdiskGuestTest
    : public GuestTest<ZirconVolatileRamdiskGuestTest> {
 public:
  static bool LaunchInfo(fuchsia::guest::LaunchInfo* launch_info) {
    create_ramdisk(kBlockSectorSize, kVirtioBlockCount, ramdisk_path_);
    launch_info->url = kZirconGuestUrl;
    launch_info->args.push_back("--display=none");
    launch_info->args.push_back("--cpus=1");
    launch_info->args.push_back(
        fxl::StringPrintf("--block=%s,volatile", ramdisk_path_));
    return true;
  }

  static bool SetUpGuest() {
    if (WaitForPkgfs() != ZX_OK) {
      ADD_FAILURE() << "Failed to wait for pkgfs";
      return false;
    }
    return true;
  }

  static char ramdisk_path_[PATH_MAX];
};

char ZirconVolatileRamdiskGuestTest::ramdisk_path_[PATH_MAX] = "";

TEST_F(ZirconVolatileRamdiskGuestTest, BlockDeviceExists) {
  ASSERT_EQ(WaitForPkgfs(), ZX_OK);
  std::string cmd = fxl::StringPrintf("run %s#%s %lu %u check", kTestUtilsUrl,
                                      kVirtioBlockUtilCmx, kBlockSectorSize,
                                      kVirtioBlockCount);
  std::string result;
  EXPECT_EQ(Execute(cmd, &result), ZX_OK);
  EXPECT_THAT(result, HasSubstr("PASS"));
}

TEST_F(ZirconVolatileRamdiskGuestTest, Read) {
  ASSERT_EQ(WaitForPkgfs(), ZX_OK);
  fbl::unique_fd fd(open(ramdisk_path_, O_RDWR));
  ASSERT_TRUE(fd);

  uint8_t data[kBlockSectorSize];
  memset(data, 0xab, kBlockSectorSize);
  for (off_t offset = 0; offset != kVirtioBlockCount;
       offset += kVirtioTestStep) {
    ASSERT_EQ(
        pwrite(fd.get(), &data, kBlockSectorSize, offset * kBlockSectorSize),
        static_cast<ssize_t>(kBlockSectorSize));
    std::string cmd = fxl::StringPrintf(
        "run %s#%s %lu %u read %d %d", kTestUtilsUrl, kVirtioBlockUtilCmx,
        kBlockSectorSize, kVirtioBlockCount, static_cast<int>(offset), 0xab);
    std::string result;
    EXPECT_EQ(Execute(cmd, &result), ZX_OK);
    EXPECT_THAT(result, HasSubstr("PASS"));
  }
}

TEST_F(ZirconVolatileRamdiskGuestTest, Write) {
  ASSERT_EQ(WaitForPkgfs(), ZX_OK);
  fbl::unique_fd fd(open(ramdisk_path_, O_RDWR));
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
    std::string cmd = fxl::StringPrintf(
        "run %s#%s %lu %u write %d %d", kTestUtilsUrl, kVirtioBlockUtilCmx,
        kBlockSectorSize, kVirtioBlockCount, static_cast<int>(offset), 0xab);
    std::string result;
    EXPECT_EQ(Execute(cmd, &result), ZX_OK);
    EXPECT_THAT(result, HasSubstr("PASS"));

    // Check that the guest reads the written bytes from the block.
    cmd = fxl::StringPrintf("run %s#%s %lu %u read %d %d", kTestUtilsUrl,
                            kVirtioBlockUtilCmx, kBlockSectorSize,
                            kVirtioBlockCount, static_cast<int>(offset), 0xab);
    EXPECT_EQ(Execute(cmd, &result), ZX_OK);
    EXPECT_THAT(result, HasSubstr("PASS"));

    // Check that the ramdisk block contains only zero (i.e. was not written).
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

static bool write_qcow_file(int fd) {
  if (!fd) {
    return false;
  }

  QcowHeader header = kDefaultHeaderV2.HostToBigEndian();
  bool write_success = write_at(fd, &header, 0);
  if (!write_success) {
    return false;
  }

  // Convert L1 entries to big-endian
  uint64_t be_table[countof(kL2TableClusterOffsets)];
  for (size_t i = 0; i < countof(kL2TableClusterOffsets); ++i) {
    be_table[i] = HostToBigEndianTraits::Convert(kL2TableClusterOffsets[i]);
  }

  // Write L1 table.
  write_success = write_at(fd, be_table, countof(kL2TableClusterOffsets),
                           kDefaultHeaderV2.l1_table_offset);
  if (!write_success) {
    return false;
  }

  // Initialize empty L2 tables.
  for (size_t i = 0; i < countof(kL2TableClusterOffsets); ++i) {
    write_success = write_at(fd, kZeroCluster, sizeof(kZeroCluster),
                             kL2TableClusterOffsets[i]);
    if (!write_success) {
      return false;
    }
  }

  // Write L2 entry
  uint64_t l2_offset = kL2TableClusterOffsets[0];
  uint64_t data_cluster_offset = ClusterOffset(kFirstDataCluster);
  uint64_t l2_entry = HostToBigEndianTraits::Convert(data_cluster_offset);
  write_success = write_at(fd, &l2_entry, l2_offset);
  if (!write_success) {
    return false;
  }

  // Write data to cluster.
  uint8_t cluster_data[kClusterSize];
  memset(cluster_data, 0xab, sizeof(cluster_data));
  write_success = write_at(fd, cluster_data, kClusterSize, data_cluster_offset);
  if (!write_success) {
    return false;
  }

  return true;
}

class ZirconReadOnlyQcowGuestTest
    : public GuestTest<ZirconReadOnlyQcowGuestTest> {
 public:
  static bool LaunchInfo(fuchsia::guest::LaunchInfo* launch_info) {
    fbl::unique_fd fd(mkstemp(&qcow_path_[0]));
    bool qcow_success = write_qcow_file(fd.get());
    if (!qcow_success) {
      return false;
    }

    launch_info->url = kZirconGuestUrl;
    launch_info->args.push_back("--display=none");
    launch_info->args.push_back("--cpus=1");
    launch_info->args.push_back(
        fxl::StringPrintf("--block=%s,ro,qcow", qcow_path_));
    return true;
  }

  static bool SetUpGuest() {
    if (WaitForPkgfs() != ZX_OK) {
      ADD_FAILURE() << "Failed to wait for pkgfs";
      return false;
    }
    return true;
  }

  static char qcow_path_[PATH_MAX];
};

char ZirconReadOnlyQcowGuestTest::qcow_path_[PATH_MAX] =
    "/tmp/guest-test.XXXXXX";

TEST_F(ZirconReadOnlyQcowGuestTest, BlockDeviceExists) {
  std::string cmd = fxl::StringPrintf("run %s#%s %lu %u check", kTestUtilsUrl,
                                      kVirtioBlockUtilCmx, kBlockSectorSize,
                                      kVirtioQcowBlockCount);
  std::string result;
  EXPECT_EQ(Execute(cmd, &result), ZX_OK);
  EXPECT_THAT(result, HasSubstr("PASS"));
}

TEST_F(ZirconReadOnlyQcowGuestTest, ReadMappedCluster) {
  for (off_t offset = 0; offset != kClusterSize / kBlockSectorSize;
       offset += kVirtioTestStep) {
    std::string cmd = fxl::StringPrintf("run %s#%s %lu %u read %d %d",
                                        kTestUtilsUrl, kVirtioBlockUtilCmx,
                                        kBlockSectorSize, kVirtioQcowBlockCount,
                                        static_cast<int>(offset), 0xab);
    std::string result;
    EXPECT_EQ(Execute(cmd, &result), ZX_OK);
    EXPECT_THAT(result, HasSubstr("PASS"));
  }
}

TEST_F(ZirconReadOnlyQcowGuestTest, ReadUnmappedCluster) {
  for (off_t offset = kClusterSize;
       offset != kClusterSize + (kClusterSize / kBlockSectorSize);
       offset += kVirtioTestStep) {
    std::string cmd = fxl::StringPrintf(
        "run %s#%s %lu %u read %d %d", kTestUtilsUrl, kVirtioBlockUtilCmx,
        kBlockSectorSize, kVirtioQcowBlockCount, static_cast<int>(offset), 0);
    std::string result;
    EXPECT_EQ(Execute(cmd, &result), ZX_OK);
    EXPECT_THAT(result, HasSubstr("PASS"));
  }
}

TEST_F(ZirconReadOnlyQcowGuestTest, Write) {
  for (off_t offset = kClusterSize;
       offset != kClusterSize + (kClusterSize / kBlockSectorSize);
       offset += kVirtioTestStep) {
    std::string cmd = fxl::StringPrintf("run %s#%s %lu %u write %d %d",
                                        kTestUtilsUrl, kVirtioBlockUtilCmx,
                                        kBlockSectorSize, kVirtioQcowBlockCount,
                                        static_cast<int>(offset), 0xab);
    std::string result;
    EXPECT_EQ(Execute(cmd, &result), ZX_OK);
    EXPECT_THAT(result, HasSubstr("PASS"));

    cmd = fxl::StringPrintf("run %s#%s %lu %u read %d %d", kTestUtilsUrl,
                            kVirtioBlockUtilCmx, kBlockSectorSize,
                            kVirtioQcowBlockCount, static_cast<int>(offset), 0);
    EXPECT_EQ(Execute(cmd, &result), ZX_OK);
    EXPECT_THAT(result, HasSubstr("PASS"));
  }
}

class ZirconVolatileQcowGuestTest
    : public GuestTest<ZirconVolatileQcowGuestTest> {
 public:
  static bool LaunchInfo(fuchsia::guest::LaunchInfo* launch_info) {
    fbl::unique_fd fd(mkstemp(&qcow_path_[0]));
    bool qcow_success = write_qcow_file(fd.get());
    if (!qcow_success) {
      return false;
    }

    launch_info->url = kZirconGuestUrl;
    launch_info->args.push_back("--display=none");
    launch_info->args.push_back("--cpus=1");
    launch_info->args.push_back(
        fxl::StringPrintf("--block=%s,volatile,qcow", qcow_path_));
    return true;
  }

  static bool SetUpGuest() {
    if (WaitForPkgfs() != ZX_OK) {
      ADD_FAILURE() << "Failed to wait for pkgfs";
      return false;
    }
    return true;
  }

  static char qcow_path_[PATH_MAX];
};

char ZirconVolatileQcowGuestTest::qcow_path_[PATH_MAX] =
    "/tmp/guest-test.XXXXXX";

TEST_F(ZirconVolatileQcowGuestTest, BlockDeviceExists) {
  std::string cmd = fxl::StringPrintf("run %s#%s %lu %u check", kTestUtilsUrl,
                                      kVirtioBlockUtilCmx, kBlockSectorSize,
                                      kVirtioQcowBlockCount);
  std::string result;
  EXPECT_EQ(Execute(cmd, &result), ZX_OK);
  EXPECT_THAT(result, HasSubstr("PASS"));
}

TEST_F(ZirconVolatileQcowGuestTest, ReadMappedCluster) {
  for (off_t offset = 0; offset != kClusterSize / kBlockSectorSize;
       offset += kVirtioTestStep) {
    std::string cmd = fxl::StringPrintf("run %s#%s %lu %u read %d %d",
                                        kTestUtilsUrl, kVirtioBlockUtilCmx,
                                        kBlockSectorSize, kVirtioQcowBlockCount,
                                        static_cast<int>(offset), 0xab);
    std::string result;
    EXPECT_EQ(Execute(cmd, &result), ZX_OK);
    EXPECT_THAT(result, HasSubstr("PASS"));
  }
}

TEST_F(ZirconVolatileQcowGuestTest, ReadUnmappedCluster) {
  for (off_t offset = kClusterSize;
       offset != kClusterSize + (kClusterSize / kBlockSectorSize);
       offset += kVirtioTestStep) {
    std::string cmd = fxl::StringPrintf(
        "run %s#%s %lu %u read %d %d", kTestUtilsUrl, kVirtioBlockUtilCmx,
        kBlockSectorSize, kVirtioQcowBlockCount, static_cast<int>(offset), 0);
    std::string result;
    EXPECT_EQ(Execute(cmd, &result), ZX_OK);
    EXPECT_THAT(result, HasSubstr("PASS"));
  }
}

TEST_F(ZirconVolatileQcowGuestTest, Write) {
  for (off_t offset = kClusterSize;
       offset != kClusterSize + (kClusterSize / kBlockSectorSize);
       offset += kVirtioTestStep) {
    std::string cmd = fxl::StringPrintf("run %s#%s %lu %u write %d %d",
                                        kTestUtilsUrl, kVirtioBlockUtilCmx,
                                        kBlockSectorSize, kVirtioQcowBlockCount,
                                        static_cast<int>(offset), 0xab);
    std::string result;
    EXPECT_EQ(Execute(cmd, &result), ZX_OK);
    EXPECT_THAT(result, HasSubstr("PASS"));

    cmd = fxl::StringPrintf("run %s#%s %lu %u read %d %d", kTestUtilsUrl,
                            kVirtioBlockUtilCmx, kBlockSectorSize,
                            kVirtioQcowBlockCount, static_cast<int>(offset),
                            0xab);
    EXPECT_EQ(Execute(cmd, &result), ZX_OK);
    EXPECT_THAT(result, HasSubstr("PASS"));
  }
}