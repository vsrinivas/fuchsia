// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_SCENIC_LIB_INPUT_GFX_LEGACY_CONTENDER_H_
#define SRC_UI_SCENIC_LIB_INPUT_GFX_LEGACY_CONTENDER_H_

#include <lib/fit/function.h>

#include <vector>

#include "src/ui/scenic/lib/input/gesture_contender.h"
#include "src/ui/scenic/lib/input/internal_pointer_event.h"

namespace scenic_impl::input {

// Class for incorporating gfx legacy clients with the gesture disambiguation protocol.
// Expect to create a new one for every each stream that has a legacy contender.
class GfxLegacyContender : public GestureContender {
 public:
  GfxLegacyContender(
      fit::function<void(GestureResponse)> respond,
      fit::function<void(const std::vector<InternalPointerEvent>&)> deliver_events_to_client,
      fit::function<void()> self_destruct);
  ~GfxLegacyContender() = default;

  void UpdateStream(StreamId stream_id, const InternalPointerEvent& event,
                    bool end_of_stream) override;

  void EndContest(StreamId stream_id, bool awarded_win) override;

 private:
  bool awarded_win_ = false;
  bool end_of_stream_ = false;
  std::vector<InternalPointerEvent> undelivered_events_;

  const fit::function<void(GestureResponse)> respond_;
  const fit::function<void(const std::vector<InternalPointerEvent>&)> deliver_events_to_client_;
  const fit::function<void()> self_destruct_;
};

}  // namespace scenic_impl::input

#endif  // SRC_UI_SCENIC_LIB_INPUT_GFX_LEGACY_CONTENDER_H_
