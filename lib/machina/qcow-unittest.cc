// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/machina/qcow.h"

#include <stdlib.h>
#include <unistd.h>

#include "gtest/gtest.h"
#include "lib/fxl/logging.h"

namespace machina {
namespace {

static constexpr size_t kClusterBits = 16;
static constexpr uint64_t kClusterSize = 1u << kClusterBits;
static constexpr uint64_t ClusterOffset(uint64_t cluster) {
  return cluster * kClusterSize;
}
// Allocate L1 table on cluster 1, L2 tables immediately following, then
// data clusters.
static constexpr uint64_t kL1TableOffset = ClusterOffset(1);
static constexpr uint64_t kL2TableClusterOffsets[] = {
    ClusterOffset(2),
    ClusterOffset(3),
    ClusterOffset(4),
    ClusterOffset(5),
};
static constexpr uint64_t kFirstDataCluster = 10;

static constexpr QcowHeader kDefaultHeaderV2 = {
    .magic = kQcowMagic,
    .version = 2,
    .backing_file_offset = 0,
    .backing_file_size = 0,
    .cluster_bits = kClusterBits,
    .size = 4ul * 1024 * 1024 * 1024,
    .crypt_method = 0,
    .l1_size = 4,
    .l1_table_offset = kL1TableOffset,
    .refcount_table_offset = 0,
    .refcount_table_clusters = 0,
    .nb_snapshots = 0,
    .snapshots_offset = 0,
    .incompatible_features = 0,
    .compatible_features = 0,
    .autoclear_features = 0,
    .refcount_order = 0,
    .header_length = 0,
};

static constexpr QcowHeader kDefaultHeaderV3 = {
    .magic = kQcowMagic,
    .version = 3,
    .backing_file_offset = 0,
    .backing_file_size = 0,
    .cluster_bits = kClusterBits,
    .size = 4ul * 1024 * 1024 * 1024,
    .crypt_method = 0,
    .l1_size = 4,
    .l1_table_offset = kL1TableOffset,
    .refcount_table_offset = 0,
    .refcount_table_clusters = 0,
    .nb_snapshots = 0,
    .snapshots_offset = 0,
    .incompatible_features = 0,
    .compatible_features = 0,
    .autoclear_features = 0,
    .refcount_order = 4,
    .header_length = sizeof(QcowHeader),
};

class QcowTest : public testing::Test {
 public:
  QcowTest() {}

  void SetUp() override {
    fd_.reset(mkstemp(&path_[0]));
    ASSERT_TRUE(fd_);
  }

  void WriteQcowHeader(const QcowHeader& header) {
    header_ = header;
    QcowHeader be_header = header.HostToBigEndian();
    SeekTo(0);
    Write(&be_header);
    WriteL1Table();
  }

  void WriteL1Table() {
    // Convert l1 entries to big-endian
    uint64_t be_table[countof(kL2TableClusterOffsets)];
    for (size_t i = 0; i < countof(kL2TableClusterOffsets); ++i) {
      be_table[i] = HostToBigEndianTraits::Convert(kL2TableClusterOffsets[i]);
    }

    // Write L1 table.
    SeekTo(header_.l1_table_offset);
    Write(be_table, countof(kL2TableClusterOffsets));

    // Initialize empty L2 tables.
    uint8_t cluster[kClusterSize] = {};
    for (size_t i = 0; i < countof(kL2TableClusterOffsets); ++i) {
      SeekTo(kL2TableClusterOffsets[i]);
      Write(cluster, sizeof(cluster));
    }
  }

  void SeekTo(off_t offset) {
    ASSERT_EQ(lseek(fd_.get(), offset, SEEK_SET), offset);
  }

  template <typename T>
  void Write(const T* ptr) {
    ASSERT_EQ(write(fd_.get(), ptr, sizeof(T)),
              static_cast<ssize_t>(sizeof(T)));
  }

  // Writes an array of T values at the current file location.
  template <typename T>
  void Write(const T* ptr, size_t len) {
    ASSERT_EQ(write(fd_.get(), ptr, len * sizeof(T)),
              static_cast<ssize_t>(len * sizeof(T)));
  }

 protected:
  std::string path_ = "/tmp/qcow-test.XXXXXX";
  fbl::unique_fd fd_;
  QcowHeader header_;
  QcowFile qcow_;
};

TEST_F(QcowTest, V2Load) {
  QcowFile file;
  WriteQcowHeader(kDefaultHeaderV2);
  ASSERT_EQ(file.Load(fd_.get()), ZX_OK);
}

TEST_F(QcowTest, V2IgnoreExtendedAttributes) {
  QcowFile file;

  // Write some values to the fields that do not exist with QCOW2 files.
  QcowHeader header = kDefaultHeaderV2;
  header.incompatible_features = 0xff;
  header.compatible_features = 0xff;
  header.autoclear_features = 0xff;
  header.refcount_order = 0xff;
  header.header_length = 0xff;
  WriteQcowHeader(header);

  // Load and validate the QCOW2 defaults are used.
  ASSERT_EQ(file.Load(fd_.get()), ZX_OK);
  EXPECT_EQ(file.header().incompatible_features, 0u);
  EXPECT_EQ(file.header().compatible_features, 0u);
  EXPECT_EQ(file.header().autoclear_features, 0u);
  EXPECT_EQ(file.header().refcount_order, 4u);
  EXPECT_EQ(file.header().header_length, 72u);
}

TEST_F(QcowTest, V3Load) {
  QcowFile file;
  WriteQcowHeader(kDefaultHeaderV3);
  ASSERT_EQ(file.Load(fd_.get()), ZX_OK);
}

TEST_F(QcowTest, V3RejectIncompatibleFeatures) {
  QcowFile file;
  QcowHeader header = kDefaultHeaderV3;
  header.incompatible_features = 1;
  WriteQcowHeader(header);
  ASSERT_EQ(file.Load(fd_.get()), ZX_ERR_NOT_SUPPORTED);
}

TEST_F(QcowTest, V3RejectCryptMethod) {
  QcowFile file;
  QcowHeader header = kDefaultHeaderV3;
  header.crypt_method = 1;
  WriteQcowHeader(header);
  ASSERT_EQ(file.Load(fd_.get()), ZX_ERR_NOT_SUPPORTED);
}

TEST_F(QcowTest, ReadUnmappedCluster) {
  QcowFile file;
  WriteQcowHeader(kDefaultHeaderV2);

  // The cluster is not mapped. Verify that reads return all 0's.
  uint8_t result[kClusterSize];
  memset(result, 0xff, sizeof(result));
  uint8_t expected[kClusterSize] = {};
  ASSERT_EQ(file.Load(fd_.get()), ZX_OK);
  ASSERT_EQ(file.Read(0, result, sizeof(result)), ZX_OK);
  ASSERT_EQ(memcmp(result, expected, sizeof(result)), 0);
}

TEST_F(QcowTest, ReadMappedCluster) {
  QcowFile file;
  WriteQcowHeader(kDefaultHeaderV2);

  // Write L2 entry
  uint64_t l2_offset = kL2TableClusterOffsets[0];
  uint64_t data_cluster_offset = ClusterOffset(kFirstDataCluster);
  uint64_t l2_entry = HostToBigEndianTraits::Convert(data_cluster_offset);
  SeekTo(l2_offset);
  Write(&l2_entry);

  // Write data to cluster.
  uint8_t cluster_data[kClusterSize];
  memset(cluster_data, 0xab, sizeof(cluster_data));
  SeekTo(data_cluster_offset);
  Write(cluster_data, kClusterSize);

  // Read cluster.
  uint8_t result[kClusterSize];
  ASSERT_EQ(file.Load(fd_.get()), ZX_OK);
  ASSERT_EQ(file.Read(0, result, sizeof(result)), ZX_OK);
  ASSERT_EQ(memcmp(result, cluster_data, sizeof(result)), 0);
}

TEST_F(QcowTest, RejectCompressedCluster) {
  QcowFile file;
  WriteQcowHeader(kDefaultHeaderV2);

  // Write L2 entry
  uint64_t l2_offset = kL2TableClusterOffsets[0];
  uint64_t data_cluster_offset = ClusterOffset(kFirstDataCluster);
  uint64_t l2_entry = HostToBigEndianTraits::Convert(data_cluster_offset |
                                                     kTableEntryCompressedBit);
  SeekTo(l2_offset);
  Write(&l2_entry);

  // Write data to cluster.
  uint8_t cluster_data[kClusterSize];
  memset(cluster_data, 0xab, sizeof(cluster_data));
  SeekTo(data_cluster_offset);
  Write(cluster_data, kClusterSize);

  // Attempt to read compressed cluster.
  ASSERT_EQ(file.Load(fd_.get()), ZX_OK);
  ASSERT_EQ(file.Read(0, cluster_data, sizeof(cluster_data)),
            ZX_ERR_NOT_SUPPORTED);
}

}  // namespace
}  // namespace machina
