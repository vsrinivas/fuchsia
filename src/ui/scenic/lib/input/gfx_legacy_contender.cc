// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/input/gfx_legacy_contender.h"

#include <lib/syslog/cpp/macros.h>

#include "src/lib/fxl/macros.h"

namespace scenic_impl::input {

GfxLegacyContender::GfxLegacyContender(
    fit::function<void(GestureResponse)> respond,
    fit::function<void(const std::vector<InternalPointerEvent>&)> deliver_events_to_client,
    fit::function<void()> self_destruct)
    : respond_(std::move(respond)),
      deliver_events_to_client_(std::move(deliver_events_to_client)),
      self_destruct_(std::move(self_destruct)) {}

void GfxLegacyContender::UpdateStream(StreamId stream_id, const InternalPointerEvent& event,
                                      bool is_end_of_stream) {
  is_end_of_stream_ = is_end_of_stream;
  if (awarded_win_) {
    FX_DCHECK(undelivered_events_.empty());
    deliver_events_to_client_({event});
    if (is_end_of_stream_) {
      self_destruct_();
      return;
    }
  } else {
    undelivered_events_.emplace_back(event);
    respond_(GestureResponse::kYes);
    return;
  }
}

void GfxLegacyContender::EndContest(StreamId stream_id, bool awarded_win) {
  if (awarded_win) {
    awarded_win_ = true;
    deliver_events_to_client_(undelivered_events_);
    undelivered_events_.clear();
  }

  if (!awarded_win_ || is_end_of_stream_) {
    self_destruct_();
    return;
  }
}

}  // namespace scenic_impl::input
