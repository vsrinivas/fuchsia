// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/input/gesture_contender_inspector.h"

#include <lib/async/cpp/time.h>
#include <lib/async/default.h>

namespace scenic_impl::input {

namespace {

uint64_t GetCurrentMinute() {
  auto dispatcher = async_get_default_dispatcher();
  return dispatcher ? async::Now(dispatcher).get() / zx::min(1).get() : 0;
}

}  // namespace

GestureContenderInspector::GestureContenderInspector(inspect::Node node)
    : node_(std::move(node)),
      history_stats_node_(node_.CreateLazyValues("Injection history", [this] {
        inspect::Inspector insp;
        ReportStats(insp);
        return fpromise::make_ok_promise(std::move(insp));
      })) {}

void GestureContenderInspector::OnInjectedEvents(zx_koid_t view_ref_koid, uint64_t num_events) {
  GetCurrentInspectHistory()[view_ref_koid].num_injected_events += num_events;
}

void GestureContenderInspector::OnContestDecided(zx_koid_t view_ref_koid, bool won) {
  if (won) {
    GetCurrentInspectHistory()[view_ref_koid].num_won_streams++;
  } else {
    GetCurrentInspectHistory()[view_ref_koid].num_lost_streams++;
  }
}

std::unordered_map<zx_koid_t, GestureContenderInspector::ViewHistory>&
GestureContenderInspector::GetCurrentInspectHistory() {
  const uint64_t current_minute = GetCurrentMinute();

  // Add elements to the front and pop from the back so that the newest element will be read out
  // first when we later iterate over the deque.
  if (history_.empty() || history_.front().minute_key != current_minute) {
    history_.push_front({
        .minute_key = current_minute,
    });
  }

  // Pop off everything older than |kNumMinutesOfHistory|.
  while (history_.size() > 1 &&
         history_.back().minute_key + kNumMinutesOfHistory <= current_minute) {
    history_.pop_back();
  }

  return history_.front().per_view_data;
}

void GestureContenderInspector::ReportStats(inspect::Inspector& inspector) const {
  inspect::Node node = inspector.GetRoot().CreateChild(
      "Last " + std::to_string(kNumMinutesOfHistory) + " minutes of injected events");

  ViewHistory sum;
  const uint64_t current_minute = GetCurrentMinute();
  for (const auto& [minute, per_view_data] : history_) {
    if (minute + kNumMinutesOfHistory <= current_minute) {
      break;
    }

    auto minute_node = node.CreateChild("Events at minute " + std::to_string(minute));
    for (const auto& [view_ref_koid, view_data] : per_view_data) {
      auto view_node = minute_node.CreateChild("View " + std::to_string(view_ref_koid));
      view_node.CreateUint("num_injected_events", view_data.num_injected_events, &inspector);
      view_node.CreateUint("num_won_streams", view_data.num_won_streams, &inspector);
      view_node.CreateUint("num_lost_streams", view_data.num_lost_streams, &inspector);

      inspector.emplace(std::move(view_node));

      sum.num_injected_events += view_data.num_injected_events;
      sum.num_won_streams += view_data.num_won_streams;
      sum.num_lost_streams += view_data.num_lost_streams;
    }
    inspector.emplace(std::move(minute_node));
  }

  {
    auto sum_node = node.CreateChild("Sum");
    sum_node.CreateUint("num_injected_events", sum.num_injected_events, &inspector);
    sum_node.CreateUint("num_won_streams", sum.num_won_streams, &inspector);
    sum_node.CreateUint("num_lost_streams", sum.num_lost_streams, &inspector);
    inspector.emplace(std::move(sum_node));
  }

  inspector.emplace(std::move(node));
}

}  // namespace scenic_impl::input
