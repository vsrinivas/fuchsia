// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_SCENIC_LIB_INPUT_GESTURE_CONTENDER_H_
#define SRC_UI_SCENIC_LIB_INPUT_GESTURE_CONTENDER_H_

#include <vector>

#include "src/ui/scenic/lib/input/internal_pointer_event.h"

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

using StreamId = uint64_t;

// Interface for a gesture disambiguation contender. All methods are called in response to
// a GestureArena.
class GestureContender {
 public:
  // Called whenever there's a new event for a stream.
  virtual void UpdateStream(StreamId stream_id, const InternalPointerEvent& event,
                            bool end_of_stream) = 0;
  // Called at the end of a contest. If |awarded_win| is false the GestureContender will
  // receive no further events for stream |stream_id|.
  virtual void EndContest(StreamId stream_id, bool awarded_win) = 0;
};

}  // namespace scenic_impl::input

#endif  // SRC_UI_SCENIC_LIB_INPUT_GESTURE_CONTENDER_H_
