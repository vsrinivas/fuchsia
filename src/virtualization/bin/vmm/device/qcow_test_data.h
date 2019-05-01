// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_VIRTUALIZATION_BIN_VMM_DEVICE_QCOW_TEST_DATA_H_
#define SRC_VIRTUALIZATION_BIN_VMM_DEVICE_QCOW_TEST_DATA_H_

#include "src/virtualization/bin/vmm/device/qcow.h"

namespace qcow_test_data {

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
