// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics.h"

namespace fvm {

Diagnostics::Diagnostics() {
  root_ = inspector_.GetRoot().CreateChild("fvm");
  mount_time_ = root_.CreateChild("mount_time");
  mount_time_format_version_ = mount_time_.CreateUint("format_version", 0);
  mount_time_oldest_revision_ = mount_time_.CreateUint("oldest_revision", 0);
  mount_time_slice_size_ = mount_time_.CreateUint("slice_size", 0);
  mount_time_num_slices_ = mount_time_.CreateUint("num_slices", 0);
  mount_time_partition_table_entries = mount_time_.CreateUint("partition_table_entries", 0);
  mount_time_partition_table_reserved_entries =
      mount_time_.CreateUint("partition_table_reserved_entries", 0);
  mount_time_allocation_table_entries = mount_time_.CreateUint("allocation_table_entries", 0);
  mount_time_allocation_table_reserved_entries =
      mount_time_.CreateUint("allocation_table_reserved_entries", 0);
  mount_time_num_partitions = mount_time_.CreateUint("num_partitions", 0);
  mount_time_num_reserved_slices = mount_time_.CreateUint("num_reserved_slices", 0);
}

void Diagnostics::OnMount(const OnMountArgs &args) {
  mount_time_format_version_.Set(args.format_version);
  mount_time_oldest_revision_.Set(args.oldest_revision);
  mount_time_slice_size_.Set(args.slice_size);
  mount_time_num_slices_.Set(args.num_slices);
  mount_time_partition_table_entries.Set(args.partition_table_entries);
  mount_time_partition_table_reserved_entries.Set(args.partition_table_reserved_entries);
  mount_time_allocation_table_entries.Set(args.allocation_table_entries);
  mount_time_allocation_table_reserved_entries.Set(args.allocation_table_reserved_entries);
  mount_time_num_partitions.Set(args.num_partitions);
  mount_time_num_reserved_slices.Set(args.num_reserved_slices);
}

}  // namespace fvm
