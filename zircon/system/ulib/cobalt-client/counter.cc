// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <cobalt-client/cpp/counter.h>

#include <zircon/assert.h>

#include <cstring>
#include <utility>

#include <cobalt-client/cpp/counter_internal.h>

namespace cobalt_client {
namespace internal {

RemoteCounter::RemoteCounter(const MetricOptions& metric_options)
    : BaseCounter(), metric_options_(metric_options) {
  buffer_ = 0;
}

RemoteCounter::RemoteCounter(RemoteCounter&& other) noexcept
    : BaseCounter(std::move(other)),
      buffer_(std::move(other.buffer_)),
      metric_options_(other.metric_options_) {}

bool RemoteCounter::Flush(Logger* logger) {
  // Write the current value of the counter to the buffer, and reset it to 0.
  buffer_ = this->Exchange();
  return logger->Log(metric_options_, buffer_);
}

void RemoteCounter::UndoFlush() { this->Increment(buffer_); }

}  // namespace internal

Counter::Counter(const MetricOptions& options) : remote_counter_(options) {}

Counter::Counter(const MetricOptions& options, Collector* collector)
    : remote_counter_(options), collector_(collector) {
  if (collector_ != nullptr) {
    collector_->Subscribe(&remote_counter_.value());
  }
}

Counter::Counter(const MetricOptions& options, internal::FlushInterface** flush_interface)
    : remote_counter_(options), collector_(nullptr) {
  *flush_interface = &remote_counter_.value();
}

Counter::~Counter() {
  if (collector_ != nullptr && remote_counter_.has_value()) {
    collector_->UnSubscribe(&remote_counter_.value());
  }
}

void Counter::Initialize(const MetricOptions& options, Collector* collector) {
  ZX_DEBUG_ASSERT_MSG(!remote_counter_.has_value(), "Cannot renitialize a Counter.");
  collector_ = collector;
  remote_counter_.emplace(options);
  if (collector_ != nullptr) {
    collector_->Subscribe(&remote_counter_.value());
  }
}

void Counter::Increment(Counter::Count value) {
  ZX_DEBUG_ASSERT_MSG(remote_counter_.has_value(), "Cannot call |Add| to unintialized Counter.");
  remote_counter_->Increment(value);
}

Counter::Count Counter::GetCount() const {
  ZX_DEBUG_ASSERT_MSG(remote_counter_.has_value(),
                      "Cannot call |GetCount| to unintialized Counter.");
  return remote_counter_->Load();
}

}  // namespace cobalt_client
