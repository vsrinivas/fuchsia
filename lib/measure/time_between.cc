// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/tracing/lib/measure/time_between.h"

namespace tracing {
namespace measure {
namespace {

bool IsOfSupportedType(const reader::Record::Event& event) {
  return event.type() == EventType::kInstant ||
         event.type() == EventType::kAsyncStart ||
         event.type() == EventType::kDurationBegin ||
         event.type() == EventType::kAsyncEnd ||
         event.type() == EventType::kDurationEnd;
}

bool EventMatchesSpecWithAnchor(const reader::Record::Event& event,
                                EventSpec spec,
                                Anchor anchor) {
  if (!EventMatchesSpec(event, spec)) {
    return false;
  }

  if (event.type() == EventType::kInstant) {
    return true;
  } else if (event.type() == EventType::kAsyncStart ||
             event.type() == EventType::kDurationBegin) {
    return anchor == Anchor::Begin;
  } else if (event.type() == EventType::kAsyncEnd ||
             event.type() == EventType::kDurationEnd) {
    return anchor == Anchor::End;
  } else {
    return false;
  }
}

}  // namespace

MeasureTimeBetween::MeasureTimeBetween(std::vector<TimeBetweenSpec> specs)
    : specs_(std::move(specs)) {}

bool MeasureTimeBetween::Process(const reader::Record::Event& event) {
  if (!IsOfSupportedType(event)) {
    return true;
  }

  for (const TimeBetweenSpec& spec : specs_) {
    uint64_t key = spec.id;

    if (EventMatchesSpecWithAnchor(event, spec.second_event,
                                   spec.second_anchor) &&
        pending_time_between_.count(key)) {
      AddResult(spec.id, pending_time_between_[key], event.timestamp);
      pending_time_between_.erase(key);
    }

    if (EventMatchesSpecWithAnchor(event, spec.first_event,
                                   spec.first_anchor)) {
      pending_time_between_[key] = event.timestamp;
    }
  }
  return true;
}

void MeasureTimeBetween::AddResult(uint64_t spec_id, Ticks from, Ticks to) {
  results_[spec_id].push_back(to - from);
}

}  // namespace measure
}  // namespace tracing
