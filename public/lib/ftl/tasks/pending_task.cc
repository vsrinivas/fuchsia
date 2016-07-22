// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/ftl/tasks/pending_task.h"

#include <utility>

namespace ftl {
namespace internal {

PendingTask::PendingTask(Closure closure,
                         TimePoint target_time,
                         unsigned int sequence_number)
    : closure_(std::move(closure)),
      target_time_(target_time),
      sequence_number_(sequence_number) {}

PendingTask::~PendingTask() {}

PendingTask::PendingTask(PendingTask&& other) = default;

PendingTask& PendingTask::operator=(PendingTask&& other) = default;

bool PendingTask::operator<(const PendingTask& other) const {
  if (target_time_ < other.target_time_)
    return false;
  if (target_time_ > other.target_time_)
    return true;
  return sequence_number_ > other.sequence_number_;
}

}  // namespace internal
}  // namespace ftl
