// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_SCENIC_LIB_FLATLAND_UBER_STRUCT_SYSTEM_H_
#define SRC_UI_SCENIC_LIB_FLATLAND_UBER_STRUCT_SYSTEM_H_

#include <map>
#include <mutex>
#include <queue>
#include <unordered_map>

#include "src/ui/scenic/lib/flatland/transform_handle.h"
#include "src/ui/scenic/lib/flatland/uber_struct.h"

namespace flatland {

// TODO(45932): write a bug to find a better name for this system
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

  // Queues an UberStruct for |id_pair|. Each Flatland instance can queue multiple UberStructs up
  // in the UberStructSystem by using different PresentIds alongside their single SessionId. Calls
  // to UpdateSessions() will commit UberStructs to the instance map accessible via Snapshot() when
  // the associated PresentId is presented. PresentIds must be increasing.
  void QueueUberStruct(scheduling::SchedulingIdPair id_pair,
                       std::unique_ptr<UberStruct> uber_struct);

  // Clears all UberStructs associated with a particular session |id|, including pending
  // UberStructs.
  void ClearUberStruct(scheduling::SessionId id);

  // Commits a new UberStruct to the instance map for each key/value pair in |sessions_to_update|.
  // All pending UberStructs associated each SessionId with lower PresentIds will be discarded.
  void UpdateSessions(
      const std::unordered_map<scheduling::SessionId, scheduling::PresentId>& sessions_to_update);

  // Snapshots the current map of UberStructs and returns the copy.
  UberStruct::InstanceMap Snapshot();

  // For pushing all pending UberStructs in tests.
  void ForceUpdateAllSessions();

  // For validating cleanup logic in tests.
  size_t GetPendingSize();

 private:
  // TODO(44335): The map of queues is modified on Flatland instance threads and read from the
  // render thread, producing a possible priority inversion between the two threads.
  std::mutex queues_mutex_;

  // A Present that has reached its acquire fences, but not its presentation time.
  struct PendingUberStruct {
    scheduling::PresentId present_id;
    std::shared_ptr<UberStruct> uber_struct;
  };

  // The queue of UberStructs pending for each active session.
  std::map<scheduling::SessionId, std::queue<PendingUberStruct>> pending_structs_queues_;

  // The current UberStruct for each Flatland instance.
  UberStruct::InstanceMap uber_struct_map_;
};

}  // namespace flatland

#endif  // SRC_UI_SCENIC_LIB_FLATLAND_UBER_STRUCT_SYSTEM_H_
