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
// refcount tables and finally data clusters.
//
// Note we add at least one empty cluster between adjacent structures to verify
// we don't overrun any clusters.
static constexpr uint64_t kL1TableOffset = ClusterOffset(1);
static constexpr uint64_t kL2TableClusterOffsets[] = {
    ClusterOffset(3),
    ClusterOffset(5),
    ClusterOffset(7),
    ClusterOffset(9),
};
static constexpr uint64_t kRefcountTableOffset = ClusterOffset(11);
static constexpr uint64_t kRefcountBlockClusterOffsets[] = {
    ClusterOffset(13),
    ClusterOffset(15),
    ClusterOffset(17),
    ClusterOffset(19),
};
static constexpr uint64_t kFirstDataCluster = 21;

// These are empty clusters that are skipped when interacting with the file.
// They should not be read from or written to.
static constexpr uint64_t kPaddingClusterOffsets[] = {
    // clang-format off
    ClusterOffset(2),
    ClusterOffset(4),
    ClusterOffset(6),
    ClusterOffset(8),
    ClusterOffset(10),
    ClusterOffset(12),
    ClusterOffset(14),
    ClusterOffset(16),
    ClusterOffset(18),
    // clang-format on
};

static constexpr uint8_t kZeroCluster[kClusterSize] = {};

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
    .refcount_table_offset = kRefcountTableOffset,
    .refcount_table_clusters = 1,
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
    .refcount_table_offset = kRefcountTableOffset,
    .refcount_table_clusters = 1,
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

  void TearDown() override { VerifyPaddingClustersAreEmpty(); }

  void VerifyPaddingClustersAreEmpty() {
    uint8_t cluster[kClusterSize];
    for (size_t i = 0; i < countof(kPaddingClusterOffsets); ++i) {
      SeekTo(kPaddingClusterOffsets[i]);
      ASSERT_EQ(read(fd_.get(), cluster, kClusterSize),
                static_cast<int>(kClusterSize));
      ASSERT_EQ(memcmp(cluster, kZeroCluster, kClusterSize), 0);
    }
  }

  void WriteQcowHeader(const QcowHeader& header) {
    header_ = header;
    QcowHeader be_header = header.HostToBigEndian();
    SeekTo(0);
    Write(&be_header);
    WriteL1Table();
    WriteRefcountTable();
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
    for (size_t i = 0; i < countof(kL2TableClusterOffsets); ++i) {
      SeekTo(kL2TableClusterOffsets[i]);
      Write(kZeroCluster, sizeof(kZeroCluster));
    }
  }

  void WriteRefcountTable() {
    // Convert entries to big-endian
    uint64_t be_table[countof(kRefcountBlockClusterOffsets)];
    for (size_t i = 0; i < countof(kRefcountBlockClusterOffsets); ++i) {
      be_table[i] =
          HostToBigEndianTraits::Convert(kRefcountBlockClusterOffsets[i]);
    }

    // Write refcount table
    SeekTo(header_.refcount_table_offset);
    Write(be_table, countof(kRefcountBlockClusterOffsets));

    // Initialize empty refcount blocks.
    for (size_t i = 0; i < countof(kRefcountBlockClusterOffsets); ++i) {
      SeekTo(kRefcountBlockClusterOffsets[i]);
      Write(kZeroCluster, sizeof(kZeroCluster));
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
  QcowFile file_;
};

TEST_F(QcowTest, V2Load) {
  WriteQcowHeader(kDefaultHeaderV2);
  ASSERT_EQ(file_.Load(fd_.get()), ZX_OK);
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
  ASSERT_EQ(file_.Load(fd_.get()), ZX_OK);
  EXPECT_EQ(file_.header().incompatible_features, 0u);
  EXPECT_EQ(file_.header().compatible_features, 0u);
  EXPECT_EQ(file_.header().autoclear_features, 0u);
  EXPECT_EQ(file_.header().refcount_order, 4u);
  EXPECT_EQ(file_.header().header_length, 72u);
}

TEST_F(QcowTest, V3Load) {
  WriteQcowHeader(kDefaultHeaderV3);
  ASSERT_EQ(file_.Load(fd_.get()), ZX_OK);
}

TEST_F(QcowTest, V3RejectIncompatibleFeatures) {
  QcowHeader header = kDefaultHeaderV3;
  header.incompatible_features = 1;
  WriteQcowHeader(header);
  ASSERT_EQ(file_.Load(fd_.get()), ZX_ERR_NOT_SUPPORTED);
}

TEST_F(QcowTest, V3RejectCryptMethod) {
  QcowHeader header = kDefaultHeaderV3;
  header.crypt_method = 1;
  WriteQcowHeader(header);
  ASSERT_EQ(file_.Load(fd_.get()), ZX_ERR_NOT_SUPPORTED);
}

TEST_F(QcowTest, ReadUnmappedCluster) {
  WriteQcowHeader(kDefaultHeaderV2);

  // The cluster is not mapped. Verify that reads return all 0's.
  uint8_t result[kClusterSize];
  memset(result, 0xff, sizeof(result));
  uint8_t expected[kClusterSize] = {};
  ASSERT_EQ(file_.Load(fd_.get()), ZX_OK);
  ASSERT_EQ(file_.Read(0, result, sizeof(result)), ZX_OK);
  ASSERT_EQ(memcmp(result, expected, sizeof(result)), 0);
}

TEST_F(QcowTest, ReadMappedCluster) {
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
  ASSERT_EQ(file_.Load(fd_.get()), ZX_OK);
  ASSERT_EQ(file_.Read(0, result, sizeof(result)), ZX_OK);
  ASSERT_EQ(memcmp(result, cluster_data, sizeof(result)), 0);
}

TEST_F(QcowTest, RejectCompressedCluster) {
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
  ASSERT_EQ(file_.Load(fd_.get()), ZX_OK);
  ASSERT_EQ(file_.Read(0, cluster_data, sizeof(cluster_data)),
            ZX_ERR_NOT_SUPPORTED);
}

}  // namespace
}  // namespace machina
