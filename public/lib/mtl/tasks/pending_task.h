// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_MTL_TASKS_PENDING_TASK_H_
#define LIB_MTL_TASKS_PENDING_TASK_H_

#include <vector>

#include "lib/ftl/functional/closure.h"
#include "lib/ftl/time/time_point.h"

namespace mtl {
namespace internal {

class PendingTask {
 public:
  PendingTask(ftl::Closure closure,
              ftl::TimePoint target_time,
              unsigned int sequence_number);
  ~PendingTask();

  PendingTask(PendingTask&& other);
  PendingTask& operator=(PendingTask&& other);

  // This |operator<| is really temporal |>| (with ties broken by
  // sequence_number) so that the pending task with the soonest target time
  // rises to the top of the priority queue.
  bool operator<(const PendingTask& other) const;

  const ftl::Closure& closure() const { return closure_; }
  ftl::TimePoint target_time() const { return target_time_; }
  unsigned int sequence_number() const { return sequence_number_; }

 private:
  ftl::Closure closure_;
  ftl::TimePoint target_time_;
  unsigned int sequence_number_;
};

using TaskQueue = std::vector<PendingTask>;

}  // namespace internal
}  // namespace mtl

#endif  // LIB_MTL_TASKS_PENDING_TASK_H_
