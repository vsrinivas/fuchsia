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

class ZirconRamdiskGuestTest : public GuestTest<ZirconRamdiskGuestTest> {
 public:
  static bool LaunchInfo(fuchsia::guest::LaunchInfo* launch_info) {
    create_ramdisk(kBlockSectorSize, kVirtioBlockCount, ramdisk_path_);
    launch_info->url = kZirconGuestUrl;
    launch_info->args.push_back("--display=none");
    launch_info->args.push_back("--cpus=1");
    launch_info->args.push_back(fxl::StringPrintf("--block=%s", ramdisk_path_));
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

char ZirconRamdiskGuestTest::ramdisk_path_[PATH_MAX] = "";

TEST_F(ZirconRamdiskGuestTest, BlockDeviceExists) {
  ASSERT_EQ(WaitForPkgfs(), ZX_OK);
  std::string cmd = fxl::StringPrintf("run %s#%s check %lu %u", kTestUtilsUrl,
                                      kVirtioBlockUtilCmx, kBlockSectorSize,
                                      kVirtioBlockCount);
  std::string result;
  EXPECT_EQ(Execute(cmd, &result), ZX_OK);
  EXPECT_THAT(result, HasSubstr("PASS"));
}

TEST_F(ZirconRamdiskGuestTest, Read) {
  ASSERT_EQ(WaitForPkgfs(), ZX_OK);
  fbl::unique_fd fd(open(ramdisk_path_, O_RDWR));
  ASSERT_TRUE(fd);

  uint8_t data[kBlockSectorSize];
  for (off_t offset = 0; offset != kVirtioBlockCount;
       offset += kVirtioTestStep) {
    memset(data, offset, kBlockSectorSize);
    ASSERT_EQ(
        pwrite(fd.get(), &data, kBlockSectorSize, offset * kBlockSectorSize),
        static_cast<ssize_t>(kBlockSectorSize));
    std::string cmd = fxl::StringPrintf(
        "run %s#%s read %lu %u %d %d", kTestUtilsUrl, kVirtioBlockUtilCmx,
        kBlockSectorSize, kVirtioBlockCount, static_cast<int>(offset),
        static_cast<int>(offset));
    std::string result;
    EXPECT_EQ(Execute(cmd, &result), ZX_OK);
    EXPECT_THAT(result, HasSubstr("PASS"));
  }
}

TEST_F(ZirconRamdiskGuestTest, Write) {
  ASSERT_EQ(WaitForPkgfs(), ZX_OK);
  fbl::unique_fd fd(open(ramdisk_path_, O_RDWR));
  ASSERT_TRUE(fd);

  uint8_t data[kBlockSectorSize];
  for (off_t offset = 0; offset != kVirtioBlockCount;
       offset += kVirtioTestStep) {
    std::string cmd = fxl::StringPrintf(
        "run %s#%s write %lu %u %d %d", kTestUtilsUrl, kVirtioBlockUtilCmx,
        kBlockSectorSize, kVirtioBlockCount, static_cast<int>(offset),
        static_cast<int>(offset));
    std::string result;
    EXPECT_EQ(Execute(cmd, &result), ZX_OK);
    EXPECT_THAT(result, HasSubstr("PASS"));
    ASSERT_EQ(
        pread(fd.get(), &data, kBlockSectorSize, offset * kBlockSectorSize),
        static_cast<ssize_t>(kBlockSectorSize));
    for (off_t i = 0; i != kBlockSectorSize; ++i) {
      EXPECT_EQ(data[i], offset);
    }
  }
}

class ZirconQcowGuestTest : public GuestTest<ZirconQcowGuestTest> {
 public:
  static bool LaunchInfo(fuchsia::guest::LaunchInfo* launch_info) {
    fbl::unique_fd fd(mkstemp(&qcow_path_[0]));
    if (!fd) {
      return false;
    }

    QcowHeader header = kDefaultHeaderV2.HostToBigEndian();
    bool write_success = WriteAt(fd.get(), &header, 0);
    if (!write_success) {
      return false;
    }

    // Convert L1 entries to big-endian
    uint64_t be_table[countof(kL2TableClusterOffsets)];
    for (size_t i = 0; i < countof(kL2TableClusterOffsets); ++i) {
      be_table[i] = HostToBigEndianTraits::Convert(kL2TableClusterOffsets[i]);
    }

    // Write L1 table.
    write_success = WriteAt(fd.get(), be_table, countof(kL2TableClusterOffsets),
                            kDefaultHeaderV2.l1_table_offset);
    if (!write_success) {
      return false;
    }

    // Initialize empty L2 tables.
    for (size_t i = 0; i < countof(kL2TableClusterOffsets); ++i) {
      write_success = WriteAt(fd.get(), kZeroCluster, sizeof(kZeroCluster),
                              kL2TableClusterOffsets[i]);
      if (!write_success) {
        return false;
      }
    }

    // Write L2 entry
    uint64_t l2_offset = kL2TableClusterOffsets[0];
    uint64_t data_cluster_offset = ClusterOffset(kFirstDataCluster);
    uint64_t l2_entry = HostToBigEndianTraits::Convert(data_cluster_offset);
    write_success = WriteAt(fd.get(), &l2_entry, l2_offset);
    if (!write_success) {
      return false;
    }

    // Write data to cluster.
    uint8_t cluster_data[kClusterSize];
    memset(cluster_data, 0xab, sizeof(cluster_data));
    write_success =
        WriteAt(fd.get(), cluster_data, kClusterSize, data_cluster_offset);
    if (!write_success) {
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

  template <typename T>
  static bool WriteAt(int fd, const T* ptr, off_t off) {
    ssize_t written = pwrite(fd, ptr, sizeof(T), off);
    return written == static_cast<ssize_t>(sizeof(T));
  }

  // Writes an array of T values at the current file location.
  template <typename T>
  static bool WriteAt(int fd, const T* ptr, size_t len, off_t off) {
    ssize_t written = pwrite(fd, ptr, len * sizeof(T), off);
    return written == static_cast<ssize_t>(len * sizeof(T));
  }

  static char qcow_path_[PATH_MAX];
};

char ZirconQcowGuestTest::qcow_path_[PATH_MAX] = "/tmp/guest-test.XXXXXX";

TEST_F(ZirconQcowGuestTest, BlockDeviceExists) {
  std::string cmd = fxl::StringPrintf("run %s#%s check %lu %u", kTestUtilsUrl,
                                      kVirtioBlockUtilCmx, kBlockSectorSize,
                                      kVirtioQcowBlockCount);
  std::string result;
  EXPECT_EQ(Execute(cmd, &result), ZX_OK);
  EXPECT_THAT(result, HasSubstr("PASS"));
}

TEST_F(ZirconQcowGuestTest, ReadMappedCluster) {
  for (off_t offset = 0; offset != kClusterSize / kBlockSectorSize;
       offset += kVirtioTestStep) {
    std::string cmd = fxl::StringPrintf("run %s#%s read %lu %u %d %d",
                                        kTestUtilsUrl, kVirtioBlockUtilCmx,
                                        kBlockSectorSize, kVirtioQcowBlockCount,
                                        static_cast<int>(offset), 0xab);
    std::string result;
    EXPECT_EQ(Execute(cmd, &result), ZX_OK);
    EXPECT_THAT(result, HasSubstr("PASS"));
  }
}

TEST_F(ZirconQcowGuestTest, ReadUnmappedCluster) {
  for (off_t offset = kClusterSize;
       offset != kClusterSize + (kClusterSize / kBlockSectorSize);
       offset += kVirtioTestStep) {
    std::string cmd = fxl::StringPrintf(
        "run %s#%s read %lu %u %d %d", kTestUtilsUrl, kVirtioBlockUtilCmx,
        kBlockSectorSize, kVirtioQcowBlockCount, static_cast<int>(offset), 0);
    std::string result;
    EXPECT_EQ(Execute(cmd, &result), ZX_OK);
    EXPECT_THAT(result, HasSubstr("PASS"));
  }
}