// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/forensics/testing/stubs/cobalt_logger.h"

namespace forensics {
namespace stubs {
namespace {

using fuchsia::metrics::Status;

}  // namespace

void CobaltLoggerBase::InsertEvent(cobalt::EventType event_type, uint32_t metric_id,
                                   std::vector<uint32_t> event_codes, uint64_t count) {
  MarkMethodAsCalled(event_type);
  events_.push_back(cobalt::Event(event_type, metric_id, event_codes, count));
}

void CobaltLogger::LogOccurrence(uint32_t metric_id, uint64_t count,
                                 ::std::vector<uint32_t> event_codes,
                                 LogOccurrenceCallback callback) {
  InsertEvent(cobalt::EventType::kOccurrence, metric_id, event_codes, count);
  callback(Status::OK);
}

void CobaltLogger::LogInteger(uint32_t metric_id, int64_t value,
                              ::std::vector<uint32_t> event_codes, LogIntegerCallback callback) {
  InsertEvent(cobalt::EventType::kInteger, metric_id, event_codes, value);
  callback(Status::OK);
}

void CobaltLoggerIgnoresFirstEvents::LogOccurrence(uint32_t metric_id, uint64_t count,
                                                   ::std::vector<uint32_t> event_codes,
                                                   LogOccurrenceCallback callback) {
  if (call_idx_++ >= ignore_call_count_) {
    InsertEvent(cobalt::EventType::kOccurrence, metric_id, event_codes, count);
    callback(Status::OK);
  }
}

}  // namespace stubs
}  // namespace forensics
