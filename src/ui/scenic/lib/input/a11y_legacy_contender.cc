// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/input/a11y_legacy_contender.h"

#include <lib/syslog/cpp/macros.h>

#include "src/lib/fxl/macros.h"

namespace scenic_impl {
namespace input {

A11yLegacyContender::A11yLegacyContender(
    fit::function<void(StreamId, GestureResponse)> respond,
    fit::function<void(const InternalPointerEvent& event)> deliver_to_client)
    : respond_(std::move(respond)), deliver_to_client_(std::move(deliver_to_client)) {}

A11yLegacyContender::~A11yLegacyContender() {
  // Reject all ongoing streams.
  for (const auto& [stream_id, stream] : ongoing_streams_) {
    respond_(stream_id, GestureResponse::kNo);
  }
}

void A11yLegacyContender::UpdateStream(StreamId stream_id, const InternalPointerEvent& event,
                                       bool is_end_of_stream) {
  deliver_to_client_(event);

  // If new stream, add it.
  if (!ongoing_streams_.count(stream_id)) {
    AddStream(stream_id, event.pointer_id);
  }

  // Check whether we're done.
  auto& stream = ongoing_streams_[stream_id];
  ++stream.num_received_events;
  stream.has_ended = is_end_of_stream;
  if (stream.has_ended && stream.awarded_win) {
    RemoveStream(stream_id);
    return;
  }

  // !consumed, we deliberately hold off responding until OnStreamHandled
  // consumed && awarded_win, no need to respond
  if (stream.consumed && !stream.awarded_win) {
    respond_(stream_id, GestureResponse::kYesPrioritize);
  }
}

void A11yLegacyContender::EndContest(StreamId stream_id, bool awarded_win) {
  FX_DCHECK(ongoing_streams_.count(stream_id) != 0);
  ongoing_streams_.at(stream_id).awarded_win = awarded_win;
  if (!awarded_win || (awarded_win && ongoing_streams_.at(stream_id).has_ended)) {
    RemoveStream(stream_id);
  }
}

void A11yLegacyContender::OnStreamHandled(
    uint32_t pointer_id, fuchsia::ui::input::accessibility::EventHandling handled) {
  if (!pointer_id_to_stream_id_map_.count(pointer_id) ||
      pointer_id_to_stream_id_map_.at(pointer_id).empty()) {
    FX_LOGS(ERROR) << "Event for unknown pointer_id received. Either a11y unexpectedly lost the "
                      "contest, or a11y sent an unexpected event.";
    return;
  }

  const StreamId stream_id = pointer_id_to_stream_id_map_.at(pointer_id).front();
  // Stream is handled. Any future responses on this pointer id will be to the next stream.
  pointer_id_to_stream_id_map_.at(pointer_id).pop_front();

  FX_DCHECK(ongoing_streams_.count(stream_id));
  switch (handled) {
    case fuchsia::ui::input::accessibility::EventHandling::CONSUMED:
      ongoing_streams_.at(stream_id).consumed = true;
      for (uint64_t i = 0; i < ongoing_streams_.at(stream_id).num_received_events; ++i) {
        respond_(stream_id, GestureResponse::kYesPrioritize);
        // respond_() may trigger a call to EndContest(). If that happened we can return (whether we
        // won or lost).
        if (ongoing_streams_.count(stream_id) == 0 || ongoing_streams_.at(stream_id).awarded_win)
          return;
      }
      break;
    case fuchsia::ui::input::accessibility::EventHandling::REJECTED:
      respond_(stream_id, GestureResponse::kNo);
      break;
    default:
      FX_LOGS(ERROR) << "Unknown fuchsia::ui::input::accessibility::EventHandling enum received. "
                        "Rejecting stream.";
      respond_(stream_id, GestureResponse::kNo);
      break;
  };
}

void A11yLegacyContender::AddStream(StreamId stream_id, uint32_t pointer_id) {
  const auto [it, success] = ongoing_streams_.emplace(stream_id, Stream{.pointer_id = pointer_id});
  FX_DCHECK(success);
  pointer_id_to_stream_id_map_[pointer_id].push_back(stream_id);
}

void A11yLegacyContender::RemoveStream(StreamId stream_id) {
  FX_DCHECK(ongoing_streams_.count(stream_id));
  const auto pointer_id = ongoing_streams_.at(stream_id).pointer_id;
  ongoing_streams_.erase(stream_id);

  auto id_kv = pointer_id_to_stream_id_map_.find(pointer_id);
  if (id_kv != pointer_id_to_stream_id_map_.end()) {
    auto& queue = id_kv->second;
    for (auto it = queue.begin(); it != queue.end(); ++it) {
      if (*it == stream_id) {
        queue.erase(it);
        break;
      }
    }
    if (queue.empty()) {
      pointer_id_to_stream_id_map_.erase(pointer_id);
    }
  }
}

}  // namespace input
}  // namespace scenic_impl
