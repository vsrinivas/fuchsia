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
    case trace::EventType::kFlowBegin:
      return ProcessAsyncOrFlowBegin(event);
    case trace::EventType::kAsyncEnd:
    case trace::EventType::kFlowEnd:
      return ProcessAsyncOrFlowEnd(event);
    case trace::EventType::kDurationBegin:
      return ProcessDurationBegin(event);
    case trace::EventType::kDurationEnd:
      return ProcessDurationEnd(event);
    default:
      return true;
  }
}

MeasureDuration::PendingBeginKey MeasureDuration::MakeKey(
    const trace::Record::Event& event) {
  PendingBeginKey key;
  key.category = event.category;
  key.name = event.name;
  switch (event.type()) {
    case trace::EventType::kAsyncBegin:
      key.type = PendingBeginKey::Type::Async;
      key.id = event.data.GetAsyncBegin().id;
      break;
    case trace::EventType::kAsyncEnd:
      key.type = PendingBeginKey::Type::Async;
      key.id = event.data.GetAsyncEnd().id;
      break;
    case trace::EventType::kFlowBegin:
      key.type = PendingBeginKey::Type::Flow;
      key.id = event.data.GetFlowBegin().id;
      break;
    case trace::EventType::kFlowEnd:
      key.type = PendingBeginKey::Type::Flow;
      key.id = event.data.GetFlowEnd().id;
      break;
    default:
      FXL_NOTREACHED();
  }
  return key;
}

bool MeasureDuration::ProcessAsyncOrFlowBegin(
    const trace::Record::Event& event) {
  const PendingBeginKey key = MakeKey(event);
  if (pending_begins_.count(key)) {
    FXL_LOG(WARNING)
        << "Ignoring a trace event: duplicate async or flow begin event";
    return false;
  }
  pending_begins_[key] = event.timestamp;
  return true;
}

bool MeasureDuration::ProcessAsyncOrFlowEnd(const trace::Record::Event& event) {
  const PendingBeginKey key = MakeKey(event);
  if (pending_begins_.count(key) == 0) {
    FXL_LOG(WARNING)
        << "Ignoring a trace event: async or flow end not preceded by begin.";
    return false;
  }

  const auto begin_timestamp = pending_begins_[key];
  pending_begins_.erase(key);
  for (const DurationSpec& spec : specs_) {
    if (!EventMatchesSpec(event, spec.event)) {
      continue;
    }

    AddResult(spec.id, begin_timestamp, event.timestamp);
  }
  return true;
}

bool MeasureDuration::ProcessDurationBegin(const trace::Record::Event& event) {
  FXL_DCHECK(event.type() == trace::EventType::kDurationBegin);
  duration_stacks_[event.process_thread].push(event.timestamp);
  return true;
}

bool MeasureDuration::ProcessDurationEnd(const trace::Record::Event& event) {
  FXL_DCHECK(event.type() == trace::EventType::kDurationEnd);
  const auto key = event.process_thread;
  if (duration_stacks_.count(key) == 0 || duration_stacks_[key].empty()) {
    FXL_LOG(WARNING)
        << "Ignoring trace event " << event.category.c_str() << ":"
        << event.name.c_str() << " @" << event.timestamp
        << ": duration end not matched by a previous duration begin.";
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

void MeasureDuration::AddResult(uint64_t spec_id, trace_ticks_t from,
                                trace_ticks_t to) {
  results_[spec_id].push_back(to - from);
}

bool MeasureDuration::PendingBeginKey::operator<(
    const PendingBeginKey& other) const {
  if (type != other.type) {
    return type < other.type;
  }
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
