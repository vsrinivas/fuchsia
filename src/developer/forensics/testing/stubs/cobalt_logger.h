// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FORENSICS_TESTING_STUBS_COBALT_LOGGER_H_
#define SRC_DEVELOPER_FORENSICS_TESTING_STUBS_COBALT_LOGGER_H_

#include <fuchsia/metrics/cpp/fidl.h>
#include <fuchsia/metrics/cpp/fidl_test_base.h>

#include <set>
#include <utility>

#include "src/developer/forensics/testing/stubs/fidl_server.h"
#include "src/developer/forensics/utils/cobalt/event.h"

namespace forensics {
namespace stubs {

// Defines the interface all stub loggers must implement and provides common functionality.
class CobaltLoggerBase
    : public SINGLE_BINDING_STUB_FIDL_SERVER(fuchsia::metrics, MetricEventLogger) {
 public:
  virtual ~CobaltLoggerBase() = default;

  const cobalt::Event& LastEvent() const { return events_.back(); }
  const std::vector<cobalt::Event>& Events() const { return events_; }

  bool WasMethodCalled(cobalt::EventType method) const { return calls_.count(method); }

 protected:
  void InsertEvent(cobalt::EventType event_type, uint32_t metric_id,
                   std::vector<uint32_t> event_codes, uint64_t count);

  void MarkMethodAsCalled(cobalt::EventType method) { calls_.insert(method); }

 private:
  std::vector<cobalt::Event> events_;
  std::set<cobalt::EventType> calls_;
};

// Always record |metric_id| and |event_code| and call callback with |Status::OK|.
class CobaltLogger : public CobaltLoggerBase {
 public:
  // |fuchsia::metrics::MetricEventLogger|
  void LogOccurrence(uint32_t metric_id, uint64_t count, ::std::vector<uint32_t> event_codes,
                     LogOccurrenceCallback callback) override;

  void LogInteger(uint32_t metric_id, int64_t value, ::std::vector<uint32_t> event_codes,
                  LogIntegerCallback callback) override;

  void LogIntegerHistogram(uint32_t metric_id,
                           ::std::vector<::fuchsia::metrics::HistogramBucket> histogram,
                           ::std::vector<uint32_t> event_codes,
                           LogIntegerHistogramCallback callback) override {
    // Not Implemented
    callback(fpromise::error(fuchsia::metrics::Error::INVALID_ARGUMENTS));
  }

  void LogString(uint32_t metric_id, ::std::string string_value, ::std::vector<uint32_t> events,
                 LogStringCallback callback) override {
    // Not Implemented
    callback(fpromise::error(fuchsia::metrics::Error::INVALID_ARGUMENTS));
  }
};

// Will not execute the callback for the first n events.
class CobaltLoggerIgnoresFirstEvents : public CobaltLoggerBase {
 public:
  explicit CobaltLoggerIgnoresFirstEvents(int n) : ignore_call_count_(n) {}

  // |fuchsia::metrics::MetricEventLogger|
  void LogOccurrence(uint32_t metric_id, uint64_t count, ::std::vector<uint32_t> event_codes,
                     LogOccurrenceCallback callback) override;

 private:
  int ignore_call_count_;
  int call_idx_ = 0;
};

}  // namespace stubs
}  // namespace forensics

#endif  // SRC_DEVELOPER_FORENSICS_TESTING_STUBS_COBALT_LOGGER_H_
