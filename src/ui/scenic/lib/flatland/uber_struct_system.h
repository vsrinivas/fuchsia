// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_SCENIC_LIB_FLATLAND_UBER_STRUCT_SYSTEM_H_
#define SRC_UI_SCENIC_LIB_FLATLAND_UBER_STRUCT_SYSTEM_H_

#include <mutex>
#include <optional>
#include <queue>
#include <unordered_map>

#include "src/ui/scenic/lib/flatland/transform_handle.h"
#include "src/ui/scenic/lib/flatland/uber_struct.h"

namespace flatland {

// TODO(fxbug.dev/45932): write a bug to find a better name for this system
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

  // An UberStruct that has not been published to the visible snapshot and the PresentId it is
  // associated with.
  struct PendingUberStruct {
    scheduling::PresentId present_id;
    std::unique_ptr<UberStruct> uber_struct;
  };

  // An interface for UberStructSystem clients to queue UberStructs to be published into the
  // visible snapshot.
  class UberStructQueue {
   public:
    // Queues an UberStruct for |present_id|. Each Flatland instance can queue multiple UberStructs
    // in the UberStructSystem by using different PresentIds. PresentIds must be increasing between
    // subsequent calls.
    void Push(scheduling::PresentId present_id, std::unique_ptr<UberStruct> uber_struct);

    // Pops a PendingUberStruct off of this Queue. If the queue is currently empty, returns
    // std::nullopt.
    std::optional<PendingUberStruct> Pop();

    // Returns the number of PendingUberStructs in this queue.
    size_t GetPendingSize();

   private:
    // TODO(fxbug.dev/57745): add a lock-free queue.
    // Protects access to |pending_structs_|.
    std::mutex queue_mutex_;
    std::queue<PendingUberStruct> pending_structs_;
  };

  // Allocates an UberStructQueue for |session_id| and returns a shared reference to that
  // UberStructQueue. Callers should call |RemoveSession| when the session associated with that
  // |session_id| has exited to clean up the allocated resources.
  std::shared_ptr<UberStructQueue> AllocateQueueForSession(scheduling::SessionId session_id);

  // Removes the UberStructQueue and current UberStruct associated with |session_id|. Any
  // PendingUberStructs pushed into the queue after this call will never be published to the
  // InstanceMap.
  void RemoveSession(scheduling::SessionId session_id);

  // Commits a new UberStruct to the instance map for each key/value pair in |sessions_to_update|.
  // All pending UberStructs associated each SessionId with lower PresentIds will be discarded.
  void UpdateSessions(
      const std::unordered_map<scheduling::SessionId, scheduling::PresentId>& sessions_to_update);

  // Snapshots the current map of UberStructs and returns the copy.
  UberStruct::InstanceMap Snapshot();

  // For pushing all pending UberStructs in tests.
  void ForceUpdateAllSessions(size_t max_updates_per_queue = 10);

  // For validating cleanup logic in tests.
  size_t GetSessionCount();

  // For getting Flatland InstanceIds in tests.
  TransformHandle::InstanceId GetLatestInstanceId() const;

 private:
  // The queue of UberStructs pending for each active session. Flatland instances push UberStructs
  // onto these queues using |UberStructQueue::Push()|. This UberStructSystem removes entries using
  // |UberStructQueue::Pop()|. Both of those operations are threadsafe, but the map itself is only
  // modified from a single thread.
  std::unordered_map<scheduling::SessionId, std::shared_ptr<UberStructQueue>>
      pending_structs_queues_;

  // The current UberStruct for each Flatland instance.
  UberStruct::InstanceMap uber_struct_map_;

  // The InstanceId most recently returned from GetNextInstanceId().
  TransformHandle::InstanceId latest_instance_id_;
};

}  // namespace flatland

#endif  // SRC_UI_SCENIC_LIB_FLATLAND_UBER_STRUCT_SYSTEM_H_
