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

  uint64_t ReadRefcount(QcowRefcount* refcount_table, uint32_t cluster) {
    uint64_t refcount;
    EXPECT_EQ(ZX_OK, refcount_table->ReadRefcount(cluster, &refcount));
    return refcount;
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

TEST_F(QcowTest, ReadWriteRefcountOrder0) {
  QcowHeader header = kDefaultHeaderV3;
  header.refcount_order = 0;
  WriteQcowHeader(header);

  // 1 bit refcount fields.
  uint64_t refcount_block_offset = kRefcountBlockClusterOffsets[0];
  uint32_t refcount_block[] = {
      0xf0f0f0f0,
      0x0f0f0f0f,
  };
  SeekTo(refcount_block_offset);
  Write(refcount_block, countof(refcount_block));

  // Read refcount.
  ASSERT_EQ(file_.Load(fd_.get()), ZX_OK);
  EXPECT_EQ(0u, ReadRefcount(file_.refcount_table(), 0));
  EXPECT_EQ(0u, ReadRefcount(file_.refcount_table(), 1));
  EXPECT_EQ(0u, ReadRefcount(file_.refcount_table(), 2));
  EXPECT_EQ(0u, ReadRefcount(file_.refcount_table(), 3));
  EXPECT_EQ(1u, ReadRefcount(file_.refcount_table(), 4));
  EXPECT_EQ(1u, ReadRefcount(file_.refcount_table(), 5));
  EXPECT_EQ(1u, ReadRefcount(file_.refcount_table(), 6));
  EXPECT_EQ(1u, ReadRefcount(file_.refcount_table(), 7));

  EXPECT_EQ(1u, ReadRefcount(file_.refcount_table(), 32));
  EXPECT_EQ(1u, ReadRefcount(file_.refcount_table(), 33));
  EXPECT_EQ(1u, ReadRefcount(file_.refcount_table(), 34));
  EXPECT_EQ(1u, ReadRefcount(file_.refcount_table(), 35));
  EXPECT_EQ(0u, ReadRefcount(file_.refcount_table(), 36));
  EXPECT_EQ(0u, ReadRefcount(file_.refcount_table(), 37));
  EXPECT_EQ(0u, ReadRefcount(file_.refcount_table(), 38));
  EXPECT_EQ(0u, ReadRefcount(file_.refcount_table(), 39));

  EXPECT_EQ(ZX_OK, file_.refcount_table()->WriteRefcount(0, 1));
  EXPECT_EQ(ZX_OK, file_.refcount_table()->WriteRefcount(1, 0));
  EXPECT_EQ(ZX_OK, file_.refcount_table()->WriteRefcount(2, 1));
  EXPECT_EQ(ZX_OK, file_.refcount_table()->WriteRefcount(3, 0));
  EXPECT_EQ(1u, ReadRefcount(file_.refcount_table(), 0));
  EXPECT_EQ(0u, ReadRefcount(file_.refcount_table(), 1));
  EXPECT_EQ(1u, ReadRefcount(file_.refcount_table(), 2));
  EXPECT_EQ(0u, ReadRefcount(file_.refcount_table(), 3));
}

TEST_F(QcowTest, ReadWriteRefcountOrder1) {
  QcowHeader header = kDefaultHeaderV3;
  header.refcount_order = 1;
  WriteQcowHeader(header);

  // 2 bit refcount fields.
  uint64_t refcount_block_offset = kRefcountBlockClusterOffsets[0];
  uint32_t refcount_block[] = {
      0xf0f0f0f0,
      0x0f0f0f0f,
  };
  SeekTo(refcount_block_offset);
  Write(refcount_block, countof(refcount_block));

  // Read refcount.
  ASSERT_EQ(file_.Load(fd_.get()), ZX_OK);
  EXPECT_EQ(0u, ReadRefcount(file_.refcount_table(), 0));
  EXPECT_EQ(0u, ReadRefcount(file_.refcount_table(), 1));
  EXPECT_EQ(3u, ReadRefcount(file_.refcount_table(), 2));
  EXPECT_EQ(3u, ReadRefcount(file_.refcount_table(), 3));
  EXPECT_EQ(0u, ReadRefcount(file_.refcount_table(), 4));
  EXPECT_EQ(0u, ReadRefcount(file_.refcount_table(), 5));
  EXPECT_EQ(3u, ReadRefcount(file_.refcount_table(), 6));
  EXPECT_EQ(3u, ReadRefcount(file_.refcount_table(), 7));

  EXPECT_EQ(3u, ReadRefcount(file_.refcount_table(), 16));
  EXPECT_EQ(3u, ReadRefcount(file_.refcount_table(), 17));
  EXPECT_EQ(0u, ReadRefcount(file_.refcount_table(), 18));
  EXPECT_EQ(0u, ReadRefcount(file_.refcount_table(), 19));
  EXPECT_EQ(3u, ReadRefcount(file_.refcount_table(), 20));
  EXPECT_EQ(3u, ReadRefcount(file_.refcount_table(), 21));
  EXPECT_EQ(0u, ReadRefcount(file_.refcount_table(), 22));
  EXPECT_EQ(0u, ReadRefcount(file_.refcount_table(), 23));

  EXPECT_EQ(ZX_OK, file_.refcount_table()->WriteRefcount(0, 1));
  EXPECT_EQ(ZX_OK, file_.refcount_table()->WriteRefcount(1, 2));
  EXPECT_EQ(ZX_OK, file_.refcount_table()->WriteRefcount(2, 3));
  EXPECT_EQ(ZX_OK, file_.refcount_table()->WriteRefcount(3, 0));
  EXPECT_EQ(1u, ReadRefcount(file_.refcount_table(), 0));
  EXPECT_EQ(2u, ReadRefcount(file_.refcount_table(), 1));
  EXPECT_EQ(3u, ReadRefcount(file_.refcount_table(), 2));
  EXPECT_EQ(0u, ReadRefcount(file_.refcount_table(), 3));
}

TEST_F(QcowTest, ReadWriteRefcountOrder2) {
  QcowHeader header = kDefaultHeaderV3;
  header.refcount_order = 2;
  WriteQcowHeader(header);

  // 4 bit refcount fields.
  uint64_t refcount_block_offset = kRefcountBlockClusterOffsets[0];
  uint32_t refcount_block[] = {
      0x76543210,
      0xfedcba98,
  };
  SeekTo(refcount_block_offset);
  Write(refcount_block, countof(refcount_block));

  // Read refcount.
  ASSERT_EQ(file_.Load(fd_.get()), ZX_OK);
  EXPECT_EQ(0x0u, ReadRefcount(file_.refcount_table(), 0));
  EXPECT_EQ(0x1u, ReadRefcount(file_.refcount_table(), 1));
  EXPECT_EQ(0x2u, ReadRefcount(file_.refcount_table(), 2));
  EXPECT_EQ(0x3u, ReadRefcount(file_.refcount_table(), 3));
  EXPECT_EQ(0x4u, ReadRefcount(file_.refcount_table(), 4));
  EXPECT_EQ(0x5u, ReadRefcount(file_.refcount_table(), 5));
  EXPECT_EQ(0x6u, ReadRefcount(file_.refcount_table(), 6));
  EXPECT_EQ(0x7u, ReadRefcount(file_.refcount_table(), 7));
  EXPECT_EQ(0x8u, ReadRefcount(file_.refcount_table(), 8));
  EXPECT_EQ(0x9u, ReadRefcount(file_.refcount_table(), 9));
  EXPECT_EQ(0xau, ReadRefcount(file_.refcount_table(), 10));
  EXPECT_EQ(0xbu, ReadRefcount(file_.refcount_table(), 11));
  EXPECT_EQ(0xcu, ReadRefcount(file_.refcount_table(), 12));
  EXPECT_EQ(0xdu, ReadRefcount(file_.refcount_table(), 13));
  EXPECT_EQ(0xeu, ReadRefcount(file_.refcount_table(), 14));
  EXPECT_EQ(0xfu, ReadRefcount(file_.refcount_table(), 15));

  EXPECT_EQ(ZX_OK, file_.refcount_table()->WriteRefcount(0, 0xa));
  EXPECT_EQ(ZX_OK, file_.refcount_table()->WriteRefcount(1, 0xb));
  EXPECT_EQ(ZX_OK, file_.refcount_table()->WriteRefcount(2, 0x7));
  EXPECT_EQ(ZX_OK, file_.refcount_table()->WriteRefcount(3, 0x6));
  EXPECT_EQ(0xau, ReadRefcount(file_.refcount_table(), 0));
  EXPECT_EQ(0xbu, ReadRefcount(file_.refcount_table(), 1));
  EXPECT_EQ(0x7u, ReadRefcount(file_.refcount_table(), 2));
  EXPECT_EQ(0x6u, ReadRefcount(file_.refcount_table(), 3));
}

TEST_F(QcowTest, ReadWriteRefcountOrder3) {
  QcowHeader header = kDefaultHeaderV3;
  header.refcount_order = 3;
  WriteQcowHeader(header);

  // 8 bit refcount fields.
  uint64_t refcount_block_offset = kRefcountBlockClusterOffsets[0];
  uint32_t refcount_block[] = {
      0x33221100,
      0x77665544,
      0xbbaa9988,
      0xffeeddcc,
  };
  SeekTo(refcount_block_offset);
  Write(refcount_block, countof(refcount_block));

  // Read refcount.
  ASSERT_EQ(file_.Load(fd_.get()), ZX_OK);
  EXPECT_EQ(0x00u, ReadRefcount(file_.refcount_table(), 0));
  EXPECT_EQ(0x11u, ReadRefcount(file_.refcount_table(), 1));
  EXPECT_EQ(0x22u, ReadRefcount(file_.refcount_table(), 2));
  EXPECT_EQ(0x33u, ReadRefcount(file_.refcount_table(), 3));
  EXPECT_EQ(0x44u, ReadRefcount(file_.refcount_table(), 4));
  EXPECT_EQ(0x55u, ReadRefcount(file_.refcount_table(), 5));
  EXPECT_EQ(0x66u, ReadRefcount(file_.refcount_table(), 6));
  EXPECT_EQ(0x77u, ReadRefcount(file_.refcount_table(), 7));
  EXPECT_EQ(0x88u, ReadRefcount(file_.refcount_table(), 8));
  EXPECT_EQ(0x99u, ReadRefcount(file_.refcount_table(), 9));
  EXPECT_EQ(0xaau, ReadRefcount(file_.refcount_table(), 10));
  EXPECT_EQ(0xbbu, ReadRefcount(file_.refcount_table(), 11));
  EXPECT_EQ(0xccu, ReadRefcount(file_.refcount_table(), 12));
  EXPECT_EQ(0xddu, ReadRefcount(file_.refcount_table(), 13));
  EXPECT_EQ(0xeeu, ReadRefcount(file_.refcount_table(), 14));
  EXPECT_EQ(0xffu, ReadRefcount(file_.refcount_table(), 15));

  EXPECT_EQ(ZX_OK, file_.refcount_table()->WriteRefcount(0, 0xfe));
  EXPECT_EQ(ZX_OK, file_.refcount_table()->WriteRefcount(1, 0xed));
  EXPECT_EQ(ZX_OK, file_.refcount_table()->WriteRefcount(2, 0x12));
  EXPECT_EQ(ZX_OK, file_.refcount_table()->WriteRefcount(3, 0x34));
  EXPECT_EQ(0xfeu, ReadRefcount(file_.refcount_table(), 0));
  EXPECT_EQ(0xedu, ReadRefcount(file_.refcount_table(), 1));
  EXPECT_EQ(0x12u, ReadRefcount(file_.refcount_table(), 2));
  EXPECT_EQ(0x34u, ReadRefcount(file_.refcount_table(), 3));
}

TEST_F(QcowTest, ReadWriteRefcountOrder4) {
  QcowHeader header = kDefaultHeaderV3;
  header.refcount_order = 4;
  WriteQcowHeader(header);

  // 16 bit refcount fields.
  uint64_t refcount_block_offset = kRefcountBlockClusterOffsets[0];
  uint16_t refcount_block[] = {
      htobe16(0x1234), htobe16(0x5678), htobe16(0x1111), htobe16(0x7654),
      htobe16(0x8888), htobe16(0x1212), htobe16(0x2121), htobe16(0x3333),
  };
  SeekTo(refcount_block_offset);
  Write(refcount_block, countof(refcount_block));

  // Read refcount.
  ASSERT_EQ(file_.Load(fd_.get()), ZX_OK);
  EXPECT_EQ(0x1234u, ReadRefcount(file_.refcount_table(), 0));
  EXPECT_EQ(0x5678u, ReadRefcount(file_.refcount_table(), 1));
  EXPECT_EQ(0x1111u, ReadRefcount(file_.refcount_table(), 2));
  EXPECT_EQ(0x7654u, ReadRefcount(file_.refcount_table(), 3));
  EXPECT_EQ(0x8888u, ReadRefcount(file_.refcount_table(), 4));
  EXPECT_EQ(0x1212u, ReadRefcount(file_.refcount_table(), 5));
  EXPECT_EQ(0x2121u, ReadRefcount(file_.refcount_table(), 6));
  EXPECT_EQ(0x3333u, ReadRefcount(file_.refcount_table(), 7));

  EXPECT_EQ(ZX_OK, file_.refcount_table()->WriteRefcount(0, 0xfeed));
  EXPECT_EQ(ZX_OK, file_.refcount_table()->WriteRefcount(1, 0xd00d));
  EXPECT_EQ(ZX_OK, file_.refcount_table()->WriteRefcount(2, 0xcafe));
  EXPECT_EQ(ZX_OK, file_.refcount_table()->WriteRefcount(3, 0xda1e));
  EXPECT_EQ(0xfeedu, ReadRefcount(file_.refcount_table(), 0));
  EXPECT_EQ(0xd00du, ReadRefcount(file_.refcount_table(), 1));
  EXPECT_EQ(0xcafeu, ReadRefcount(file_.refcount_table(), 2));
  EXPECT_EQ(0xda1eu, ReadRefcount(file_.refcount_table(), 3));
}

TEST_F(QcowTest, ReadWriteRefcountOrder5) {
  QcowHeader header = kDefaultHeaderV3;
  header.refcount_order = 5;
  WriteQcowHeader(header);

  // 32 bit refcount fields.
  uint64_t refcount_block_offset = kRefcountBlockClusterOffsets[0];
  uint32_t refcount_block[] = {
      htobe32(0x01234567), htobe32(0x89abcdef), htobe32(0x11111111),
      htobe32(0x76543210), htobe32(0x88888888), htobe32(0x12121212),
      htobe32(0x21212121), htobe32(0x33333333),
  };
  SeekTo(refcount_block_offset);
  Write(refcount_block, countof(refcount_block));

  // Read refcount.
  ASSERT_EQ(file_.Load(fd_.get()), ZX_OK);
  EXPECT_EQ(0x01234567u, ReadRefcount(file_.refcount_table(), 0));
  EXPECT_EQ(0x89abcdefu, ReadRefcount(file_.refcount_table(), 1));
  EXPECT_EQ(0x11111111u, ReadRefcount(file_.refcount_table(), 2));
  EXPECT_EQ(0x76543210u, ReadRefcount(file_.refcount_table(), 3));
  EXPECT_EQ(0x88888888u, ReadRefcount(file_.refcount_table(), 4));
  EXPECT_EQ(0x12121212u, ReadRefcount(file_.refcount_table(), 5));
  EXPECT_EQ(0x21212121u, ReadRefcount(file_.refcount_table(), 6));
  EXPECT_EQ(0x33333333u, ReadRefcount(file_.refcount_table(), 7));

  EXPECT_EQ(ZX_OK, file_.refcount_table()->WriteRefcount(0, 0xfeed1234));
  EXPECT_EQ(ZX_OK, file_.refcount_table()->WriteRefcount(1, 0xd00d5432));
  EXPECT_EQ(ZX_OK, file_.refcount_table()->WriteRefcount(2, 0xcafe8888));
  EXPECT_EQ(ZX_OK, file_.refcount_table()->WriteRefcount(3, 0xda1e2222));
  EXPECT_EQ(0xfeed1234u, ReadRefcount(file_.refcount_table(), 0));
  EXPECT_EQ(0xd00d5432u, ReadRefcount(file_.refcount_table(), 1));
  EXPECT_EQ(0xcafe8888u, ReadRefcount(file_.refcount_table(), 2));
  EXPECT_EQ(0xda1e2222u, ReadRefcount(file_.refcount_table(), 3));
}

TEST_F(QcowTest, ReadWriteRefcountOrder6) {
  QcowHeader header = kDefaultHeaderV3;
  header.refcount_order = 6;
  WriteQcowHeader(header);

  // 64 bit refcount fields.
  uint64_t refcount_block_offset = kRefcountBlockClusterOffsets[0];
  uint64_t refcount_block[] = {
      htobe64(0x0123456789abcdef), htobe64(0xfedcba9876543210),
      htobe64(0x1111111111111111), htobe64(0x7654321076543210),
      htobe64(0x8888888888888888), htobe64(0x1212121212121212),
      htobe64(0x2121212121212121), htobe64(0x3333333333333333),
  };
  SeekTo(refcount_block_offset);
  Write(refcount_block, countof(refcount_block));

  // Read refcount.
  ASSERT_EQ(file_.Load(fd_.get()), ZX_OK);
  EXPECT_EQ(0x0123456789abcdefu, ReadRefcount(file_.refcount_table(), 0));
  EXPECT_EQ(0xfedcba9876543210u, ReadRefcount(file_.refcount_table(), 1));
  EXPECT_EQ(0x1111111111111111u, ReadRefcount(file_.refcount_table(), 2));
  EXPECT_EQ(0x7654321076543210u, ReadRefcount(file_.refcount_table(), 3));
  EXPECT_EQ(0x8888888888888888u, ReadRefcount(file_.refcount_table(), 4));
  EXPECT_EQ(0x1212121212121212u, ReadRefcount(file_.refcount_table(), 5));
  EXPECT_EQ(0x2121212121212121u, ReadRefcount(file_.refcount_table(), 6));
  EXPECT_EQ(0x3333333333333333u, ReadRefcount(file_.refcount_table(), 7));

  EXPECT_EQ(ZX_OK,
            file_.refcount_table()->WriteRefcount(0, 0x0123456701234567));
  EXPECT_EQ(ZX_OK,
            file_.refcount_table()->WriteRefcount(1, 0xaaaaaaaaaaaaaaaa));
  EXPECT_EQ(ZX_OK,
            file_.refcount_table()->WriteRefcount(2, 0x1231231231231231));
  EXPECT_EQ(ZX_OK,
            file_.refcount_table()->WriteRefcount(3, 0x0000000000000000));
  EXPECT_EQ(0x0123456701234567u, ReadRefcount(file_.refcount_table(), 0));
  EXPECT_EQ(0xaaaaaaaaaaaaaaaau, ReadRefcount(file_.refcount_table(), 1));
  EXPECT_EQ(0x1231231231231231u, ReadRefcount(file_.refcount_table(), 2));
  EXPECT_EQ(0x0000000000000000u, ReadRefcount(file_.refcount_table(), 3));
}

}  // namespace
}  // namespace machina
