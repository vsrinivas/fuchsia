// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_SCENIC_LIB_FLATLAND_UBER_STRUCT_SYSTEM_H_
#define SRC_UI_SCENIC_LIB_FLATLAND_UBER_STRUCT_SYSTEM_H_

#include <mutex>
#include <unordered_map>

#include "src/ui/scenic/lib/flatland/transform_handle.h"
#include "src/ui/scenic/lib/flatland/uber_struct.h"

namespace flatland {

// TODO(#####): write a bug to find a better name for this system
//
// A system for aggregating local data from Flatland instances to be consumed by the render loop.
// All functions are thread safe. The intent is for separate worker threads to own each Flatland
// instance, compute local data (such as topology vectors) in their local thread, and then commit
// those vectors to this class in a concurrent manner.
class UberStructSystem {
 public:
  UberStructSystem() = default;

  // Returns the next instance ID for this particular UberStructSystem. Instance IDs are guaranteed
  // to be unique for each caller and should be used as keys for setting UberStructs and accessing
  // UberStructs in snapshots.
  TransformHandle::InstanceId GetNextInstanceId();

  // Sets the UberStruct for |id|. Each instance ID many only have one UberStruct committed to the
  // system at a time, so calling SetUberStruct again will override the existing value.
  void SetUberStruct(TransformHandle::InstanceId id, std::unique_ptr<UberStruct> uber_struct);

  // Clears an UberStruct from the system.
  void ClearUberStruct(TransformHandle::InstanceId id);

  // Snapshots the current map of UberStructs and returns the copy.
  std::unordered_map<TransformHandle::InstanceId, std::shared_ptr<UberStruct>> Snapshot();

  // For validating cleanup logic in tests.
  size_t GetSize();

 private:
  std::atomic<TransformHandle::InstanceId> next_graph_id_ = 0;

  // TODO(44335): This map is modified by Flatland instances when Flatland::Present() is called, and
  // read within ComputeGlobalTopologyVector() on the render thread, producing a possible priority
  // inversion between the two threads.
  std::mutex map_mutex_;
  std::unordered_map<TransformHandle::InstanceId, std::shared_ptr<UberStruct>> uber_struct_map_;
};

}  // namespace flatland

#endif  // SRC_UI_SCENIC_LIB_FLATLAND_UBER_STRUCT_SYSTEM_H_
