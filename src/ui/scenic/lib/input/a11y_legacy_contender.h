// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_SCENIC_LIB_INPUT_A11Y_LEGACY_CONTENDER_H_
#define SRC_UI_SCENIC_LIB_INPUT_A11Y_LEGACY_CONTENDER_H_

#include <fuchsia/ui/input/accessibility/cpp/fidl.h>
#include <lib/fit/function.h>

#include <deque>
#include <unordered_map>

#include "src/ui/scenic/lib/input/gesture_contender.h"
#include "src/ui/scenic/lib/input/internal_pointer_event.h"

namespace scenic_impl::input {

// Class for incorporating a11y legacy clients with the gesture disambiguation protocol.
class A11yLegacyContender final : public GestureContender {
 public:
  A11yLegacyContender(fit::function<void(StreamId, GestureResponse)> respond,
                      fit::function<void(const InternalPointerEvent& event)> deliver_to_client);
  ~A11yLegacyContender();

  void UpdateStream(StreamId stream_id, const InternalPointerEvent& event,
                    bool is_end_of_stream) override;

  void EndContest(StreamId stream_id, bool awarded_win) override;

  // Implementation of |fuchsia::ui::input::accessibility::PointerEventListener::OnStreamHandled|.
  void OnStreamHandled(uint32_t pointer_id,
                       fuchsia::ui::input::accessibility::EventHandling handled);

 private:
  struct Stream {
    bool consumed = false;
    bool has_ended = false;
    bool awarded_win = false;
    uint32_t pointer_id = 0;
    uint64_t num_received_events = 0;
  };

  void AddStream(StreamId stream_id, uint32_t pointer_id);
  void RemoveStream(StreamId stream_id);

  std::unordered_map<StreamId, Stream> ongoing_streams_;
  // Multiple streams with the same pointer id can start before A11y has time to respond to the
  // previous one. Handle them in order, since A11y responses should arrive in order.
  std::unordered_map</*pointer_id*/ uint32_t, std::deque<StreamId>> pointer_id_to_stream_id_map_;

  const fit::function<void(StreamId, GestureResponse)> respond_;
  const fit::function<void(const InternalPointerEvent& event)> deliver_to_client_;
};

}  // namespace scenic_impl::input

#endif  // SRC_UI_SCENIC_LIB_INPUT_A11Y_LEGACY_CONTENDER_H_
