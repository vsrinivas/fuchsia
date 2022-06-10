// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_VIRTUALIZATION_BIN_VMM_DEVICE_QCOW_TEST_DATA_H_
#define SRC_VIRTUALIZATION_BIN_VMM_DEVICE_QCOW_TEST_DATA_H_

#include <endian.h>
#include <stddef.h>
#include <stdint.h>
#include <zircon/compiler.h>

namespace qcow_test_data {

// Each QCOW file starts with this magic value "QFI\xfb".
static constexpr uint32_t kQcowMagic = 0x514649fb;

struct BigToHostEndianTraits {
  static uint16_t Convert(uint16_t val) { return be16toh(val); }
  static uint32_t Convert(uint32_t val) { return be32toh(val); }
  static uint64_t Convert(uint64_t val) { return be64toh(val); }
};

struct HostToBigEndianTraits {
  static uint16_t Convert(uint16_t val) { return htobe16(val); }
  static uint32_t Convert(uint32_t val) { return htobe32(val); }
  static uint64_t Convert(uint64_t val) { return htobe64(val); }
};

struct QcowHeader {
  uint32_t magic;
  uint32_t version;
  uint64_t backing_file_offset;
  uint32_t backing_file_size;
  uint32_t cluster_bits;
  uint64_t size;
  uint32_t crypt_method;
  uint32_t l1_size;
  uint64_t l1_table_offset;
  uint64_t refcount_table_offset;
  uint32_t refcount_table_clusters;
  uint32_t nb_snapshots;
  uint64_t snapshots_offset;

  // Only present on version 3+
  uint64_t incompatible_features;
  uint64_t compatible_features;
  uint64_t autoclear_features;
  uint32_t refcount_order;
  uint32_t header_length;

  uint32_t cluster_size() const { return 1u << cluster_bits; }

  // Return a new header that has been converted from host-endian to big-endian.
  QcowHeader HostToBigEndian() const { return ByteSwap<HostToBigEndianTraits>(); }

  // Return a new header that has been converted from big-endian to host-endian.
  QcowHeader BigToHostEndian() const { return ByteSwap<BigToHostEndianTraits>(); }

 private:
  // Byte-swaps the members of the header using the desired scheme.
  template <typename ByteOrderTraits>
  QcowHeader ByteSwap() const {
    return QcowHeader{
        .magic = ByteOrderTraits::Convert(magic),
        .version = ByteOrderTraits::Convert(version),
        .backing_file_offset = ByteOrderTraits::Convert(backing_file_offset),
        .backing_file_size = ByteOrderTraits::Convert(backing_file_size),
        .cluster_bits = ByteOrderTraits::Convert(cluster_bits),
        .size = ByteOrderTraits::Convert(size),
        .crypt_method = ByteOrderTraits::Convert(crypt_method),
        .l1_size = ByteOrderTraits::Convert(l1_size),
        .l1_table_offset = ByteOrderTraits::Convert(l1_table_offset),
        .refcount_table_offset = ByteOrderTraits::Convert(refcount_table_offset),
        .refcount_table_clusters = ByteOrderTraits::Convert(refcount_table_clusters),
        .nb_snapshots = ByteOrderTraits::Convert(nb_snapshots),
        .snapshots_offset = ByteOrderTraits::Convert(snapshots_offset),
        .incompatible_features = ByteOrderTraits::Convert(incompatible_features),
        .compatible_features = ByteOrderTraits::Convert(compatible_features),
        .autoclear_features = ByteOrderTraits::Convert(autoclear_features),
        .refcount_order = ByteOrderTraits::Convert(refcount_order),
        .header_length = ByteOrderTraits::Convert(header_length),
    };
  }
} __PACKED;

static constexpr size_t kClusterBits = 16;
static constexpr uint64_t kClusterSize = 1u << kClusterBits;
static constexpr uint64_t ClusterOffset(uint64_t cluster) { return cluster * kClusterSize; }
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
    .l1_size = 8,
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
    .l1_size = 8,
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

}  // namespace qcow_test_data

#endif  // SRC_VIRTUALIZATION_BIN_VMM_DEVICE_QCOW_TEST_DATA_H_
