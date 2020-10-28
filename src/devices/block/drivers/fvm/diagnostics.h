// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_BLOCK_DRIVERS_FVM_DIAGNOSTICS_H_
#define SRC_DEVICES_BLOCK_DRIVERS_FVM_DIAGNOSTICS_H_

#include <lib/inspect/cpp/inspect.h>
#include <lib/inspect/cpp/vmo/types.h>
#include <lib/zx/vmo.h>

namespace fvm {

// Diagnostics exposes internal information and metrics recorded by FVM to the rest of the system
// via the Inspect API.
// This object owns a VMO which it publishes metrics into; this VMO is read by the inspect
// framework and can be accessed through (e.g.) iquery.
// This class is thread-safe and movable.
class Diagnostics {
 public:
  Diagnostics();
  ~Diagnostics() = default;
  Diagnostics(Diagnostics&& o) noexcept = default;
  Diagnostics& operator=(Diagnostics&& o) noexcept = default;
  Diagnostics(const Diagnostics& o) = delete;
  Diagnostics& operator=(const Diagnostics& o) = delete;

  struct OnMountArgs {
    // Version of the fileystem
    uint64_t format_version = 0;
    // Oldest revision driver which has touched of the fileystem
    uint64_t oldest_revision = 0;
    // Slice size (bytes)
    uint64_t slice_size = 0;
    // Number of slices
    uint64_t num_slices = 0;
    // Partition table size (number of entries)
    uint64_t partition_table_entries = 0;
    // Total number of partition entries the FVM instance can accomodate
    uint64_t partition_table_reserved_entries = 0;
    // Partition table size (number of entries)
    uint64_t allocation_table_entries = 0;
    // Total number of slice entries the FVM instance can accomodate
    uint64_t allocation_table_reserved_entries = 0;
    // Number of partitions
    uint64_t num_partitions = 0;
    // Number of slices reserved
    uint64_t num_reserved_slices = 0;
  };
  // Reports the initial state of the FVM instance. Should be called once on mount.
  void OnMount(const OnMountArgs& args);

  // Returns a read-only duplicate of the VMO this object writes to. Suitable for giving out to an
  // external process which would like to subscribe to FVM's diagnostics.
  zx::vmo DuplicateVmo() { return inspector_.DuplicateVmo(); }

 private:
  inspect::Inspector inspector_;

  // Root node. We add this in so that we can label everything with an `fvm` prefix.
  inspect::Node root_;

  // Metrics collected once at mount time.
  inspect::Node mount_time_;

  inspect::UintProperty mount_time_format_version_;
  inspect::UintProperty mount_time_oldest_revision_;
  inspect::UintProperty mount_time_slice_size_;
  inspect::UintProperty mount_time_num_slices_;
  inspect::UintProperty mount_time_partition_table_entries;
  inspect::UintProperty mount_time_partition_table_reserved_entries;
  inspect::UintProperty mount_time_allocation_table_entries;
  inspect::UintProperty mount_time_allocation_table_reserved_entries;
  inspect::UintProperty mount_time_num_partitions;
  inspect::UintProperty mount_time_num_reserved_slices;
};

}  // namespace fvm

#endif  // SRC_DEVICES_BLOCK_DRIVERS_FVM_DIAGNOSTICS_H_
