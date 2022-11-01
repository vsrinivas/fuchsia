// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_FIRMWARE_GIGABOOT_CPP_PARTITION_H_
#define SRC_FIRMWARE_GIGABOOT_CPP_PARTITION_H_

#include <lib/stdcompat/span.h>
#include <zircon/hw/gpt.h>

#include <optional>
#include <string_view>

#include <fbl/vector.h>

namespace gigaboot {
class PartitionMap {
 public:
  struct PartitionEntry {
    std::string_view name;
    size_t min_size_bytes;
    uint8_t type_guid[GPT_GUID_LEN] = {0};
  };

  // Factory function for a valid GPT partition map given custom partition definitions.
  //
  // The relative order of partitions is preserved.
  // If a partition name is repeated, the largest size it is given will be assigned.
  // If the last partition entry has a min_size_bytes value of SIZE_MAX,
  // it will take all remaining space on the device.
  // If any partition entry besides the final one has a min_size_bytes of SIZE_MAX,
  // GeneratePartitionMap will return std::nullopt
  static std::optional<PartitionMap> GeneratePartitionMap(
      cpp20::span<const PartitionEntry> const partitions);

  fbl::Vector<PartitionEntry> const& partitions() const { return partitions_; }

 private:
  PartitionMap(fbl::Vector<PartitionEntry>&& partitions) : partitions_(std::move(partitions)) {}

  fbl::Vector<PartitionEntry> partitions_;
};

}  // namespace gigaboot

#endif  // SRC_FIRMWARE_GIGABOOT_CPP_PARTITION_H_
