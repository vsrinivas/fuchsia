// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/measure/duration.h"

#include "garnet/public/lib/fxl/logging.h"

namespace tracing {
namespace measure {

MeasureDuration::MeasureDuration(std::vector<DurationSpec> specs)
    : specs_(std::move(specs)) {}

bool MeasureDuration::Process(const trace::Record::Event& event) {
  switch (event.type()) {
    case trace::EventType::kAsyncBegin:
      return ProcessAsyncStart(event);
    case trace::EventType::kAsyncEnd:
      return ProcessAsyncEnd(event);
    case trace::EventType::kDurationBegin:
      return ProcessDurationStart(event);
    case trace::EventType::kDurationEnd:
      return ProcessDurationEnd(event);
    default:
      return true;
  }
}

bool MeasureDuration::ProcessAsyncStart(const trace::Record::Event& event) {
  FXL_DCHECK(event.type() == trace::EventType::kAsyncBegin);
  const PendingAsyncKey key = {event.category, event.name,
                               event.data.GetAsyncBegin().id};
  if (pending_async_begins_.count(key)) {
    FXL_LOG(WARNING) << "Ignoring a trace event: duplicate async begin event";
    return false;
  }
  pending_async_begins_[key] = event.timestamp;
  return true;
}

bool MeasureDuration::ProcessAsyncEnd(const trace::Record::Event& event) {
  FXL_DCHECK(event.type() == trace::EventType::kAsyncEnd);

  const PendingAsyncKey key = {event.category, event.name,
                               event.data.GetAsyncEnd().id};
  if (pending_async_begins_.count(key) == 0) {
    FXL_LOG(WARNING)
        << "Ignoring a trace event: async end not preceded by async begin.";
    return false;
  }

  const auto begin_timestamp = pending_async_begins_[key];
  pending_async_begins_.erase(key);
  for (const DurationSpec& spec : specs_) {
    if (!EventMatchesSpec(event, spec.event)) {
      continue;
    }

    AddResult(spec.id, begin_timestamp, event.timestamp);
  }
  return true;
}

bool MeasureDuration::ProcessDurationStart(const trace::Record::Event& event) {
  FXL_DCHECK(event.type() == trace::EventType::kDurationBegin);
  duration_stacks_[event.process_thread].push(event.timestamp);
  return true;
}

bool MeasureDuration::ProcessDurationEnd(const trace::Record::Event& event) {
  FXL_DCHECK(event.type() == trace::EventType::kDurationEnd);
  const auto key = event.process_thread;
  if (duration_stacks_.count(key) == 0 || duration_stacks_[key].empty()) {
    FXL_LOG(WARNING)
        << "Ignoring a trace event: duration end not matched by a previous "
        << "duration begin.";
    return false;
  }

  const auto begin_timestamp = duration_stacks_[key].top();
  duration_stacks_[key].pop();
  if (duration_stacks_[key].empty()) {
    duration_stacks_.erase(key);
  }

  for (const DurationSpec& spec : specs_) {
    if (!EventMatchesSpec(event, spec.event)) {
      continue;
    }
    AddResult(spec.id, begin_timestamp, event.timestamp);
  }
  return true;
}

void MeasureDuration::AddResult(uint64_t spec_id, trace_ticks_t from, trace_ticks_t to) {
  results_[spec_id].push_back(to - from);
}

bool MeasureDuration::PendingAsyncKey::operator<(
    const PendingAsyncKey& other) const {
  if (category != other.category) {
    return category < other.category;
  }
  if (name != other.name) {
    return name < other.name;
  }
  return id < other.id;
}

}  // namespace measure
}  // namespace tracing
