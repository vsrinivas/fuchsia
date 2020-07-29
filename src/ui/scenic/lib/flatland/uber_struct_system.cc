// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/flatland/uber_struct_system.h"

#include <lib/syslog/cpp/macros.h>

namespace flatland {

TransformHandle::InstanceId UberStructSystem::GetNextInstanceId() {
  return scheduling::GetNextSessionId();
}

void UberStructSystem::QueueUberStruct(scheduling::SchedulingIdPair id_pair,
                                       std::unique_ptr<UberStruct> uber_struct) {
  FX_DCHECK(uber_struct);

  // Acquire the lock and update the appropriate queue.
  {
    std::scoped_lock lock(queues_mutex_);
    auto& queue = pending_structs_queues_[id_pair.session_id];

    // PresentIds must be strictly increasing
    FX_DCHECK(queue.empty() || queue.back().present_id < id_pair.present_id);

    queue.push({.present_id = id_pair.present_id, .uber_struct = std::move(uber_struct)});
  }
}

void UberStructSystem::ClearUberStruct(scheduling::SessionId id) {
  // Acquire the lock and remove the appropriate queue.
  {
    std::scoped_lock lock(queues_mutex_);
    pending_structs_queues_.erase(id);
    uber_struct_map_.erase(id);
  }
}

void UberStructSystem::UpdateSessions(
    const std::unordered_map<scheduling::SessionId, scheduling::PresentId>& sessions_to_update) {
  // Acquire the lock and update.
  {
    std::scoped_lock lock(queues_mutex_);

    for (const auto& id_kv : sessions_to_update) {
      scheduling::SchedulingIdPair id_pair = {id_kv.first, id_kv.second};

      // Find the queue associated with this SessonId.
      auto queue_kv = pending_structs_queues_.find(id_pair.session_id);
      FX_DCHECK(queue_kv != pending_structs_queues_.end());

      // Pop entries from that queue until the correct PresentId is found, then commit that
      // UberStruct to the queue.
      while (!queue_kv->second.empty()) {
        auto pending_uber_struct = std::move(queue_kv->second.front());
        FX_DCHECK(pending_uber_struct.present_id <= id_pair.present_id);

        queue_kv->second.pop();

        if (pending_uber_struct.present_id == id_pair.present_id) {
          uber_struct_map_[id_pair.session_id] = std::move(pending_uber_struct.uber_struct);
          break;
        }
      }
    }
  }
}

void UberStructSystem::ForceUpdateAllSessions() {
  // Acquire the lock and update.
  {
    std::scoped_lock lock(queues_mutex_);

    // Pop all entries from all queues.
    for (auto& queue_kv : pending_structs_queues_) {
      while (!queue_kv.second.empty()) {
        auto pending_uber_struct = std::move(queue_kv.second.front());
        uber_struct_map_[queue_kv.first] = std::move(pending_uber_struct.uber_struct);
        queue_kv.second.pop();
      }
    }
  }
}

UberStruct::InstanceMap UberStructSystem::Snapshot() { return uber_struct_map_; }

size_t UberStructSystem::GetPendingSize() {
  std::scoped_lock lock(queues_mutex_);
  size_t count = 0;
  for (const auto& queue_kv : pending_structs_queues_) {
    count += queue_kv.second.size();
  }
  return count;
}

}  // namespace flatland
