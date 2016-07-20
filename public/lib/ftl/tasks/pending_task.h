// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FTL_TASKS_PENDING_TASK_H_
#define LIB_FTL_TASKS_PENDING_TASK_H_

#include <vector>

#include "lib/ftl/functional/closure.h"
#include "lib/ftl/time/time_point.h"

namespace ftl {
namespace internal {

class PendingTask {
 public:
  PendingTask(Closure closure,
              TimePoint target_time,
              unsigned int sequence_number);
  ~PendingTask();

  PendingTask(PendingTask&& other);
  PendingTask& operator=(PendingTask&& other);

  // This |operator<| is really temporal |>| (with ties broken by
  // sequence_number) so that the pending task with the soonest target time
  // rises to the top of the priority queue.
  bool operator<(const PendingTask& other) const;

  const Closure& closure() const { return closure_; }
  TimePoint target_time() const { return target_time_; }
  unsigned int sequence_number() const { return sequence_number_; }

 private:
  Closure closure_;
  TimePoint target_time_;
  unsigned int sequence_number_;
};

using TaskQueue = std::vector<PendingTask>;

}  // namespace internal
}  // namespace ftl

#endif  // LIB_FTL_TASKS_PENDING_TASK_H_
