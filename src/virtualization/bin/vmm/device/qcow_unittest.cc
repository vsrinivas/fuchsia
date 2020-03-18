// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/virtualization/bin/vmm/device/qcow.h"

#include <sys/stat.h>

#include <fbl/unique_fd.h>
#include <gtest/gtest.h>

#include "src/lib/fxl/arraysize.h"
#include "src/lib/syslog/cpp/logger.h"
#include "src/virtualization/bin/vmm/device/qcow_test_data.h"

namespace {

using namespace qcow_test_data;

class FdBlockDispatcher : public BlockDispatcher {
 public:
  explicit FdBlockDispatcher(int fd) : fd_(fd) {}

 private:
  int fd_;

  void Sync(Callback callback) override {
    int ret = fsync(fd_);
    callback(ret < 0 ? ZX_ERR_IO : ZX_OK);
  }

  void ReadAt(void* data, uint64_t size, uint64_t off, Callback callback) override {
    int ret = pread(fd_, data, size, off);
    callback(ret < 0 ? ZX_ERR_IO : ZX_OK);
  }

  void WriteAt(const void* data, uint64_t size, uint64_t off, Callback callback) override {
    int ret = pwrite(fd_, data, size, off);
    callback(ret < 0 ? ZX_ERR_IO : ZX_OK);
  }
};

class QcowTest : public testing::Test {
 public:
  QcowTest() {}

  void SetUp() override {
    fd_.reset(mkstemp(&path_[0]));
    ASSERT_TRUE(fd_);
  }

  void TearDown() override { VerifyPaddingClustersAreEmpty(); }

  void VerifyPaddingClustersAreEmpty() {
    uint8_t cluster[kClusterSize];
    for (size_t i = 0; i < arraysize(kPaddingClusterOffsets); ++i) {
      ASSERT_EQ(static_cast<int>(kClusterSize),
                pread(fd_.get(), cluster, kClusterSize, kPaddingClusterOffsets[i]));
      ASSERT_EQ(0, memcmp(cluster, kZeroCluster, kClusterSize));
    }
  }

  void WriteQcowHeader(const QcowHeader& header) {
    header_ = header;
    QcowHeader be_header = header.HostToBigEndian();
    WriteAt(&be_header, 0);
    WriteL1Table();
    WriteRefcountTable();
  }

  void WriteL1Table() {
    // Convert l1 entries to big-endian
    uint64_t be_table[arraysize(kL2TableClusterOffsets)];
    for (size_t i = 0; i < arraysize(kL2TableClusterOffsets); ++i) {
      be_table[i] = HostToBigEndianTraits::Convert(kL2TableClusterOffsets[i]);
    }

    // Write L1 table.
    WriteAt(be_table, arraysize(kL2TableClusterOffsets), header_.l1_table_offset);

    // Initialize empty L2 tables.
    for (size_t i = 0; i < arraysize(kL2TableClusterOffsets); ++i) {
      WriteAt(kZeroCluster, sizeof(kZeroCluster), kL2TableClusterOffsets[i]);
    }
  }

  void WriteRefcountTable() {
    // Convert entries to big-endian
    uint64_t be_table[arraysize(kRefcountBlockClusterOffsets)];
    for (size_t i = 0; i < arraysize(kRefcountBlockClusterOffsets); ++i) {
      be_table[i] = HostToBigEndianTraits::Convert(kRefcountBlockClusterOffsets[i]);
    }

    // Write refcount table
    WriteAt(be_table, arraysize(kRefcountBlockClusterOffsets), header_.refcount_table_offset);

    // Initialize empty refcount blocks.
    for (size_t i = 0; i < arraysize(kRefcountBlockClusterOffsets); ++i) {
      WriteAt(kZeroCluster, sizeof(kZeroCluster), kRefcountBlockClusterOffsets[i]);
    }
  }

  template <typename T>
  void WriteAt(const T* ptr, off_t off) {
    ASSERT_EQ(static_cast<ssize_t>(sizeof(T)), pwrite(fd_.get(), ptr, sizeof(T), off));
  }

  // Writes an array of T values at the current file location.
  template <typename T>
  void WriteAt(const T* ptr, size_t len, off_t off) {
    ASSERT_EQ(static_cast<ssize_t>(len * sizeof(T)), pwrite(fd_.get(), ptr, len * sizeof(T), off));
  }

  zx_status_t Load() {
    FdBlockDispatcher disp(fd_.get());
    zx_status_t status;
    file_.Load(&disp, [&status](zx_status_t s) { status = s; });
    return status;
  }

  zx_status_t ReadAt(void* data, uint64_t size) {
    FdBlockDispatcher disp(fd_.get());
    zx_status_t status;
    file_.ReadAt(&disp, data, size, 0, [&status](zx_status_t s) { status = s; });
    return status;
  }

 protected:
  std::string path_ = "/tmp/qcow-test.XXXXXX";
  fbl::unique_fd fd_;
  QcowHeader header_;
  QcowFile file_;
};

TEST_F(QcowTest, V2Load) {
  WriteQcowHeader(kDefaultHeaderV2);
  ASSERT_EQ(ZX_OK, Load());
}

TEST_F(QcowTest, V2IgnoreExtendedAttributes) {
  // Write some values to the fields that do not exist with QCOW2 files.
  QcowHeader header = kDefaultHeaderV2;
  header.incompatible_features = 0xff;
  header.compatible_features = 0xff;
  header.autoclear_features = 0xff;
  header.refcount_order = 0xff;
  header.header_length = 0xff;
  WriteQcowHeader(header);

  // Load and validate the QCOW2 defaults are used.
  ASSERT_EQ(ZX_OK, Load());
  EXPECT_EQ(0u, file_.header().incompatible_features);
  EXPECT_EQ(0u, file_.header().compatible_features);
  EXPECT_EQ(0u, file_.header().autoclear_features);
  EXPECT_EQ(4u, file_.header().refcount_order);
  EXPECT_EQ(72u, file_.header().header_length);
}

TEST_F(QcowTest, RejectInvalidL1Size) {
  QcowHeader header = kDefaultHeaderV2;
  header.l1_size = 0;
  WriteQcowHeader(header);
  ASSERT_EQ(ZX_ERR_INVALID_ARGS, Load());
}

TEST_F(QcowTest, V3Load) {
  WriteQcowHeader(kDefaultHeaderV3);
  ASSERT_EQ(ZX_OK, Load());
}

TEST_F(QcowTest, V3RejectIncompatibleFeatures) {
  QcowHeader header = kDefaultHeaderV3;
  header.incompatible_features = 1;
  WriteQcowHeader(header);
  ASSERT_EQ(ZX_ERR_NOT_SUPPORTED, Load());
}

TEST_F(QcowTest, V3RejectCryptMethod) {
  QcowHeader header = kDefaultHeaderV3;
  header.crypt_method = 1;
  WriteQcowHeader(header);
  ASSERT_EQ(ZX_ERR_NOT_SUPPORTED, Load());
}

TEST_F(QcowTest, ReadUnmappedCluster) {
  WriteQcowHeader(kDefaultHeaderV2);

  // The cluster is not mapped. Verify that reads return all 0's.
  uint8_t result[kClusterSize];
  memset(result, 0xff, sizeof(result));
  uint8_t expected[kClusterSize] = {};
  ASSERT_EQ(ZX_OK, Load());
  ASSERT_EQ(ZX_OK, ReadAt(result, sizeof(result)));
  ASSERT_EQ(0, memcmp(result, expected, sizeof(result)));
}

TEST_F(QcowTest, ReadMappedCluster) {
  WriteQcowHeader(kDefaultHeaderV2);

  // Write L2 entry
  uint64_t l2_offset = kL2TableClusterOffsets[0];
  uint64_t data_cluster_offset = ClusterOffset(kFirstDataCluster);
  uint64_t l2_entry = HostToBigEndianTraits::Convert(data_cluster_offset);
  WriteAt(&l2_entry, l2_offset);

  // Write data to cluster.
  uint8_t cluster_data[kClusterSize];
  memset(cluster_data, 0xab, sizeof(cluster_data));
  WriteAt(cluster_data, kClusterSize, data_cluster_offset);

  // Read cluster.
  uint8_t result[kClusterSize];
  ASSERT_EQ(ZX_OK, Load());
  ASSERT_EQ(ZX_OK, ReadAt(result, sizeof(result)));
  ASSERT_EQ(memcmp(result, cluster_data, sizeof(result)), 0);
}

TEST_F(QcowTest, RejectCompressedCluster) {
  WriteQcowHeader(kDefaultHeaderV2);

  // Write L2 entry
  uint64_t l2_offset = kL2TableClusterOffsets[0];
  uint64_t data_cluster_offset = ClusterOffset(kFirstDataCluster);
  uint64_t l2_entry =
      HostToBigEndianTraits::Convert(data_cluster_offset | kTableEntryCompressedBit);
  WriteAt(&l2_entry, l2_offset);

  // Write data to cluster.
  uint8_t cluster_data[kClusterSize];
  memset(cluster_data, 0xab, sizeof(cluster_data));
  WriteAt(cluster_data, kClusterSize, data_cluster_offset);

  // Attempt to read compressed cluster.
  ASSERT_EQ(ZX_OK, Load());
  ASSERT_EQ(ZX_ERR_NOT_SUPPORTED, ReadAt(cluster_data, sizeof(cluster_data)));
}

}  // namespace
