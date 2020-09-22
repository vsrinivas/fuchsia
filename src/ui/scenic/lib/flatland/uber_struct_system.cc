// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/flatland/uber_struct_system.h"

#include <lib/syslog/cpp/macros.h>

namespace flatland {

// |UberStructSystem| implementations.

TransformHandle::InstanceId UberStructSystem::GetNextInstanceId() {
  // |latest_instance_id_| is only used for tests, but returning a member value can result in
  // threads "stealing" instance IDs from each other, so we return a local value here instead,
  // which does not have the same risk.
  auto next_instance_id = scheduling::GetNextSessionId();
  latest_instance_id_ = next_instance_id;
  return next_instance_id;
}

std::shared_ptr<UberStructSystem::UberStructQueue> UberStructSystem::AllocateQueueForSession(
    scheduling::SessionId session_id) {
  FX_DCHECK(!pending_structs_queues_.count(session_id));

  auto [queue_kv, success] =
      pending_structs_queues_.emplace(session_id, std::make_shared<UberStructQueue>());
  FX_DCHECK(success);

  return queue_kv->second;
}

void UberStructSystem::RemoveSession(scheduling::SessionId session_id) {
  FX_DCHECK(pending_structs_queues_.count(session_id));

  pending_structs_queues_.erase(session_id);
  uber_struct_map_.erase(session_id);
}

void UberStructSystem::UpdateSessions(
    const std::unordered_map<scheduling::SessionId, scheduling::PresentId>& sessions_to_update) {
  for (const auto& [session_id, present_id] : sessions_to_update) {
    // Find the queue associated with this SessonId. It may not exist if the SessionId is
    // associated with a GFX session instead of a Flatland one.
    auto queue_kv = pending_structs_queues_.find(session_id);
    if (queue_kv == pending_structs_queues_.end()) {
      continue;
    }

    // Pop entries from that queue until the correct PresentId is found, then commit that
    // UberStruct to the queue.
    auto pending_struct = queue_kv->second->Pop();
    while (pending_struct.has_value()) {
      FX_DCHECK(pending_struct->present_id <= present_id);

      if (pending_struct->present_id == present_id) {
        uber_struct_map_[session_id] = std::move(pending_struct->uber_struct);
        break;
      }

      pending_struct = queue_kv->second->Pop();
    }
  }
}

void UberStructSystem::ForceUpdateAllSessions(size_t max_updates_per_queue) {
  // Pop entries from each queue until empty.
  for (auto& [session_id, queue] : pending_structs_queues_) {
    size_t update_count = 0;
    auto pending_struct = queue->Pop();
    while (pending_struct.has_value()) {
      uber_struct_map_[session_id] = std::move(pending_struct.value().uber_struct);
      pending_struct = queue->Pop();

      if (++update_count > max_updates_per_queue) {
        break;
      }
    }
  }
}

UberStruct::InstanceMap UberStructSystem::Snapshot() { return uber_struct_map_; }

size_t UberStructSystem::GetSessionCount() { return pending_structs_queues_.size(); }

TransformHandle::InstanceId UberStructSystem::GetLatestInstanceId() const {
  return latest_instance_id_;
}

// |UberStructSystem::Queue| implementations.

void UberStructSystem::UberStructQueue::Push(scheduling::PresentId present_id,
                                             std::unique_ptr<UberStruct> uber_struct) {
  FX_DCHECK(uber_struct);

  // Acquire the lock and update the appropriate queue.
  {
    std::scoped_lock lock(queue_mutex_);

    // PresentIds must be strictly increasing
    FX_DCHECK(pending_structs_.empty() || pending_structs_.back().present_id < present_id);

    pending_structs_.push({.present_id = present_id, .uber_struct = std::move(uber_struct)});
  }
}

std::optional<UberStructSystem::PendingUberStruct> UberStructSystem::UberStructQueue::Pop() {
  std::optional<PendingUberStruct> pending_struct;

  // Acquire the lock and fetch the next PendingUberStruct, if there is one.
  {
    std::scoped_lock lock(queue_mutex_);
    if (pending_structs_.empty()) {
      pending_struct = std::nullopt;
    } else {
      pending_struct = std::move(pending_structs_.front());
      pending_structs_.pop();
    }
  }

  return pending_struct;
}

size_t UberStructSystem::UberStructQueue::GetPendingSize() {
  std::scoped_lock lock(queue_mutex_);
  return pending_structs_.size();
}

}  // namespace flatland
