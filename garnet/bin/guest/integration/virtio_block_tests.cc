// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fbl/unique_fd.h>
#include <fcntl.h>
#include <gmock/gmock.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/fd.h>
#include <lib/fdio/fdio.h>
#include <limits.h>
#include <src/lib/fxl/arraysize.h>
#include <src/lib/fxl/logging.h>
#include <src/lib/fxl/strings/string_printf.h>
#include <stdlib.h>
#include <string.h>

#include "garnet/bin/guest/vmm/device/block.h"
#include "garnet/bin/guest/vmm/device/qcow.h"
#include "garnet/bin/guest/vmm/device/qcow_test_data.h"
#include "guest_test.h"

using namespace qcow_test_data;
using testing::HasSubstr;

static constexpr char kVirtioBlockUtil[] = "virtio_block_test_util";
static constexpr uint32_t kVirtioBlockCount = 32;
static constexpr uint32_t kVirtioQcowBlockCount = 4 * 1024 * 1024 * 2;
static constexpr uint32_t kVirtioTestStep = 8;

template <class T>
T* GuestTest<T>::enclosed_guest_ = nullptr;

static fidl::VectorPtr<fuchsia::guest::BlockDevice> block_device(
    fuchsia::guest::BlockMode mode, fuchsia::guest::BlockFormat format,
    int fd) {
  zx_handle_t handle;
  zx_status_t status = fdio_get_service_handle(fd, &handle);
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

static zx_status_t write_raw_file(int fd) {
  int ret = ftruncate(fd, kVirtioBlockCount * kBlockSectorSize);
  if (ret != 0) {
    return ZX_ERR_IO;
  }
  return ZX_OK;
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

static zx_status_t write_qcow_file(int fd) {
  QcowHeader header = kDefaultHeaderV2.HostToBigEndian();
  bool write_success = write_at(fd, &header, 0);
  if (!write_success) {
    return ZX_ERR_IO;
  }

  // Convert L1 entries to big-endian
  uint64_t be_table[arraysize(kL2TableClusterOffsets)];
  for (size_t i = 0; i < arraysize(kL2TableClusterOffsets); ++i) {
    be_table[i] = HostToBigEndianTraits::Convert(kL2TableClusterOffsets[i]);
  }

  // Write L1 table.
  write_success = write_at(fd, be_table, arraysize(kL2TableClusterOffsets),
                           kDefaultHeaderV2.l1_table_offset);
  if (!write_success) {
    return ZX_ERR_IO;
  }

  // Initialize empty L2 tables.
  for (size_t i = 0; i < arraysize(kL2TableClusterOffsets); ++i) {
    write_success = write_at(fd, kZeroCluster, sizeof(kZeroCluster),
                             kL2TableClusterOffsets[i]);
    if (!write_success) {
      return ZX_ERR_IO;
    }
  }

  // Write L2 entry
  uint64_t l2_offset = kL2TableClusterOffsets[0];
  uint64_t data_cluster_offset = ClusterOffset(kFirstDataCluster);
  uint64_t l2_entry = HostToBigEndianTraits::Convert(data_cluster_offset);
  write_success = write_at(fd, &l2_entry, l2_offset);
  if (!write_success) {
    return ZX_ERR_IO;
  }

  // Write data to cluster.
  uint8_t cluster_data[kClusterSize];
  memset(cluster_data, 0xab, sizeof(cluster_data));
  write_success = write_at(fd, cluster_data, kClusterSize, data_cluster_offset);
  if (!write_success) {
    return ZX_ERR_IO;
  }

  return ZX_OK;
}

template <fuchsia::guest::BlockMode Mode, fuchsia::guest::BlockFormat Format>
class VirtioBlockZirconGuest : public ZirconEnclosedGuest {
 public:
  zx_status_t LaunchInfo(fuchsia::guest::LaunchInfo* launch_info) override {
    zx_status_t status = ZirconEnclosedGuest::LaunchInfo(launch_info);
    if (status != ZX_OK) {
      return status;
    }

    fbl::unique_fd fd(mkstemp(file_path_.data()));
    if (!fd) {
      FXL_LOG(ERROR) << "Failed to create temporary file";
      return ZX_ERR_IO;
    }

    status = ZX_ERR_BAD_STATE;
    if (Format == fuchsia::guest::BlockFormat::RAW) {
      status = write_raw_file(fd.get());
    } else if (Format == fuchsia::guest::BlockFormat::QCOW) {
      status = write_qcow_file(fd.get());
    }
    if (status != ZX_OK) {
      return status;
    }

    launch_info->block_devices = block_device(Mode, Format, fd.release());
    return ZX_OK;
  }

  fuchsia::guest::BlockMode BlockMode() const { return Mode; }
  const std::string& FilePath() const { return file_path_; }

  std::string file_path_ = "/tmp/guest-test.XXXXXX";
};

template <fuchsia::guest::BlockMode Mode, fuchsia::guest::BlockFormat Format>
class VirtioBlockDebianGuest : public DebianEnclosedGuest {
 public:
  zx_status_t LaunchInfo(fuchsia::guest::LaunchInfo* launch_info) override {
    zx_status_t status = DebianEnclosedGuest::LaunchInfo(launch_info);
    if (status != ZX_OK) {
      return status;
    }

    fbl::unique_fd fd(mkstemp(file_path_.data()));
    if (!fd) {
      FXL_LOG(ERROR) << "Failed to create temporary file";
      return ZX_ERR_IO;
    }

    status = ZX_ERR_BAD_STATE;
    if (Format == fuchsia::guest::BlockFormat::RAW) {
      status = write_raw_file(fd.get());
    } else if (Format == fuchsia::guest::BlockFormat::QCOW) {
      status = write_qcow_file(fd.get());
    }
    if (status != ZX_OK) {
      return status;
    }

    launch_info->block_devices = block_device(Mode, Format, fd.release());
    return ZX_OK;
  }

  fuchsia::guest::BlockMode BlockMode() const { return Mode; }
  const std::string& FilePath() const { return file_path_; }

  std::string file_path_ = "/tmp/guest-test.XXXXXX";
};

template <class T>
class VirtioBlockGuestTest : public GuestTest<T> {
 public:
  const std::string& FilePath() const {
    return this->GetEnclosedGuest()->FilePath();
  }
  fuchsia::guest::BlockMode BlockMode() const {
    return this->GetEnclosedGuest()->BlockMode();
  }
};

template <class T>
using RawVirtioBlockGuestTest = VirtioBlockGuestTest<T>;

using RawGuestTypes = ::testing::Types<
    VirtioBlockZirconGuest<fuchsia::guest::BlockMode::READ_ONLY,
                           fuchsia::guest::BlockFormat::RAW>,
    VirtioBlockZirconGuest<fuchsia::guest::BlockMode::READ_WRITE,
                           fuchsia::guest::BlockFormat::RAW>,
    VirtioBlockZirconGuest<fuchsia::guest::BlockMode::VOLATILE_WRITE,
                           fuchsia::guest::BlockFormat::RAW>,
    VirtioBlockDebianGuest<fuchsia::guest::BlockMode::READ_ONLY,
                           fuchsia::guest::BlockFormat::RAW>,
    VirtioBlockDebianGuest<fuchsia::guest::BlockMode::READ_WRITE,
                           fuchsia::guest::BlockFormat::RAW>,
    VirtioBlockDebianGuest<fuchsia::guest::BlockMode::VOLATILE_WRITE,
                           fuchsia::guest::BlockFormat::RAW>>;
TYPED_TEST_SUITE(RawVirtioBlockGuestTest, RawGuestTypes);

TYPED_TEST(RawVirtioBlockGuestTest, BlockDeviceExists) {
  std::string args =
      fxl::StringPrintf("%lu %u check", kBlockSectorSize, kVirtioBlockCount);
  std::string result;
  EXPECT_EQ(this->RunUtil(kVirtioBlockUtil, args, &result), ZX_OK);
  EXPECT_THAT(result, HasSubstr("PASS"));
}

TYPED_TEST(RawVirtioBlockGuestTest, Read) {
  fbl::unique_fd fd(open(this->FilePath().c_str(), O_RDWR));
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
    EXPECT_EQ(this->RunUtil(kVirtioBlockUtil, args, &result), ZX_OK);
    EXPECT_THAT(result, HasSubstr("PASS"));
  }
}

TYPED_TEST(RawVirtioBlockGuestTest, Write) {
  fbl::unique_fd fd(open(this->FilePath().c_str(), O_RDWR));
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
    EXPECT_EQ(this->RunUtil(kVirtioBlockUtil, args, &result), ZX_OK);

    // TODO(MAC-234): The virtio-block driver on Zircon currently doesn't inform
    // the rest of the system when the device is read only.
    if (this->GetGuestKernel() == GuestKernel::LINUX &&
        this->BlockMode() == fuchsia::guest::BlockMode::READ_ONLY) {
      EXPECT_THAT(result, HasSubstr("PermissionDenied"));
    } else {
      EXPECT_THAT(result, HasSubstr("PASS"));
    }

    int expected_guest_read, expected_host_read;
    switch (this->BlockMode()) {
      case fuchsia::guest::BlockMode::READ_ONLY:
        expected_guest_read = 0;
        expected_host_read = 0;
        break;
      case fuchsia::guest::BlockMode::READ_WRITE:
        expected_guest_read = 0xab;
        expected_host_read = 0xab;
        break;
      case fuchsia::guest::BlockMode::VOLATILE_WRITE:
        expected_guest_read = 0xab;
        expected_host_read = 0;
        break;
    }

    // Check the value when read from the guest.
    args = fxl::StringPrintf("%lu %u read %d %d", kBlockSectorSize,
                             kVirtioBlockCount, static_cast<int>(offset),
                             expected_guest_read);
    EXPECT_EQ(this->RunUtil(kVirtioBlockUtil, args, &result), ZX_OK);
    EXPECT_THAT(result, HasSubstr("PASS"));

    // Check the value when read from the host file.
    ASSERT_EQ(
        pread(fd.get(), &data, kBlockSectorSize, offset * kBlockSectorSize),
        static_cast<ssize_t>(kBlockSectorSize));
    for (off_t i = 0; i != kBlockSectorSize; ++i) {
      EXPECT_EQ(data[i], expected_host_read);
    }
  }
}

template <class T>
using QcowVirtioBlockGuestTest = VirtioBlockGuestTest<T>;

using QcowGuestTypes = ::testing::Types<
    VirtioBlockZirconGuest<fuchsia::guest::BlockMode::READ_ONLY,
                           fuchsia::guest::BlockFormat::QCOW>,
    VirtioBlockZirconGuest<fuchsia::guest::BlockMode::VOLATILE_WRITE,
                           fuchsia::guest::BlockFormat::QCOW>,
    VirtioBlockDebianGuest<fuchsia::guest::BlockMode::READ_ONLY,
                           fuchsia::guest::BlockFormat::QCOW>,
    VirtioBlockDebianGuest<fuchsia::guest::BlockMode::VOLATILE_WRITE,
                           fuchsia::guest::BlockFormat::QCOW>>;
TYPED_TEST_SUITE(QcowVirtioBlockGuestTest, QcowGuestTypes);

TYPED_TEST(QcowVirtioBlockGuestTest, BlockDeviceExists) {
  std::string args = fxl::StringPrintf("%lu %u check", kBlockSectorSize,
                                       kVirtioQcowBlockCount);
  std::string result;
  EXPECT_EQ(this->RunUtil(kVirtioBlockUtil, args, &result), ZX_OK);
  EXPECT_THAT(result, HasSubstr("PASS"));
}

TYPED_TEST(QcowVirtioBlockGuestTest, ReadMappedCluster) {
  for (off_t offset = 0; offset != kClusterSize / kBlockSectorSize;
       offset += kVirtioTestStep) {
    std::string args = fxl::StringPrintf("%lu %u read %d %d", kBlockSectorSize,
                                         kVirtioQcowBlockCount,
                                         static_cast<int>(offset), 0xab);
    std::string result;
    EXPECT_EQ(this->RunUtil(kVirtioBlockUtil, args, &result), ZX_OK);
    EXPECT_THAT(result, HasSubstr("PASS"));
  }
}

TYPED_TEST(QcowVirtioBlockGuestTest, ReadUnmappedCluster) {
  for (off_t offset = kClusterSize;
       offset != kClusterSize + (kClusterSize / kBlockSectorSize);
       offset += kVirtioTestStep) {
    std::string args =
        fxl::StringPrintf("%lu %u read %d %d", kBlockSectorSize,
                          kVirtioQcowBlockCount, static_cast<int>(offset), 0);
    std::string result;
    EXPECT_EQ(this->RunUtil(kVirtioBlockUtil, args, &result), ZX_OK);
    EXPECT_THAT(result, HasSubstr("PASS"));
  }
}

TYPED_TEST(QcowVirtioBlockGuestTest, Write) {
  for (off_t offset = kClusterSize;
       offset != kClusterSize + (kClusterSize / kBlockSectorSize);
       offset += kVirtioTestStep) {
    std::string args = fxl::StringPrintf("%lu %u write %d %d", kBlockSectorSize,
                                         kVirtioQcowBlockCount,
                                         static_cast<int>(offset), 0xab);
    std::string result;
    EXPECT_EQ(this->RunUtil(kVirtioBlockUtil, args, &result), ZX_OK);

    // TODO(MAC-234): The virtio-block driver on Zircon currently doesn't inform
    // the rest of the system when the device is read only.
    if (this->GetGuestKernel() == GuestKernel::LINUX &&
        this->BlockMode() == fuchsia::guest::BlockMode::READ_ONLY) {
      EXPECT_THAT(result, HasSubstr("PermissionDenied"));
    } else {
      EXPECT_THAT(result, HasSubstr("PASS"));
    }

    int expected_read;
    switch (this->BlockMode()) {
      case fuchsia::guest::BlockMode::READ_ONLY:
        expected_read = 0;
        break;
      case fuchsia::guest::BlockMode::VOLATILE_WRITE:
        expected_read = 0xab;
        break;
      case fuchsia::guest::BlockMode::READ_WRITE:
        // READ_WRITE not supported for QCOW.
        expected_read = -1;
        break;
    }

    args = fxl::StringPrintf("%lu %u read %d %d", kBlockSectorSize,
                             kVirtioQcowBlockCount, static_cast<int>(offset),
                             expected_read);
    EXPECT_EQ(this->RunUtil(kVirtioBlockUtil, args, &result), ZX_OK);
    EXPECT_THAT(result, HasSubstr("PASS"));
  }
}
