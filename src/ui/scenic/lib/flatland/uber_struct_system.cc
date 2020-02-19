// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/flatland/uber_struct_system.h"

#include "src/lib/fxl/logging.h"

namespace flatland {

TransformHandle::InstanceId UberStructSystem::GetNextInstanceId() { return next_graph_id_++; }

void UberStructSystem::SetUberStruct(TransformHandle::InstanceId id,
                                     std::unique_ptr<UberStruct> uber_struct) {
  FXL_DCHECK(uber_struct);

  // Acquire the lock and update.
  {
    std::scoped_lock lock(map_mutex_);
    uber_struct_map_[id] = std::move(uber_struct);
  }
}

void UberStructSystem::ClearUberStruct(TransformHandle::InstanceId id) {
  // Acquire the lock and update.
  {
    std::scoped_lock lock(map_mutex_);
    uber_struct_map_.erase(id);
  }
}

UberStruct::InstanceMap UberStructSystem::Snapshot() {
  UberStruct::InstanceMap copy;

  // Acquire the lock and copy.
  {
    std::scoped_lock lock(map_mutex_);
    copy = uber_struct_map_;
  }

  return copy;
}

size_t UberStructSystem::GetSize() {
  std::scoped_lock lock(map_mutex_);
  return uber_struct_map_.size();
}

}  // namespace flatland
