// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_SCENIC_LIB_INPUT_GESTURE_CONTENDER_H_
#define SRC_UI_SCENIC_LIB_INPUT_GESTURE_CONTENDER_H_

#include <vector>

#include "src/ui/scenic/lib/input/internal_pointer_event.h"
#include "src/ui/scenic/lib/input/stream_id.h"
#include "src/ui/scenic/lib/view_tree/snapshot_types.h"

namespace scenic_impl::input {

// Plain enum since it's used to index into a matrix.
enum GestureResponse {
  kYes = 0,
  kYesPrioritize = 1,
  kMaybe = 2,
  kMaybePrioritize = 3,
  kMaybeSuppress = 4,
  kMaybePrioritizeSuppress = 5,
  kHold = 6,
  kHoldSuppress = 7,
  kNo = 8,
  kUndefined = 9
};

using ContenderId = uint32_t;
constexpr ContenderId kInvalidContenderId = 0;

// Interface for a gesture disambiguation contender. All methods are called in response to
// a GestureArena.
class GestureContender {
 public:
  explicit GestureContender(zx_koid_t view_ref_koid) : view_ref_koid_(view_ref_koid) {}

  // Called whenever there's a new event for a stream.
  virtual void UpdateStream(StreamId stream_id, const InternalTouchEvent& event,
                            bool is_end_of_stream, view_tree::BoundingBox view_bounds) = 0;
  // Called at the end of a contest. If |awarded_win| is false the GestureContender will
  // receive no further events for stream |stream_id|.
  // If called before the first call to UpdateStream() for |stream_id|, the win message should
  // be delivered to the client along with the initial UpdateStream() event.
  virtual void EndContest(StreamId stream_id, bool awarded_win) = 0;

  const zx_koid_t view_ref_koid_;
};

}  // namespace scenic_impl::input

#endif  // SRC_UI_SCENIC_LIB_INPUT_GESTURE_CONTENDER_H_
