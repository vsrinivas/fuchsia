// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <zircon/assert.h>

#include <cstring>
#include <utility>

#include <cobalt-client/cpp/counter_internal.h>
#include <cobalt-client/cpp/integer.h>

namespace cobalt_client {
namespace internal {

RemoteInteger::RemoteInteger(const MetricOptions& metric_options)
    : BaseCounter(), metric_options_(metric_options) {
  buffer_ = 0;
}

RemoteInteger::RemoteInteger(RemoteInteger&& other) noexcept
    : BaseCounter(std::move(other)),
      buffer_(other.buffer_),
      metric_options_(other.metric_options_) {}

bool RemoteInteger::Flush(Logger* logger) {
  // Write the current value of the counter to the buffer, and reset it to 0.
  buffer_ = this->Exchange();
  return logger->LogInteger(metric_options_, buffer_);
}

void RemoteInteger::UndoFlush() { this->Increment(buffer_); }

}  // namespace internal

Integer::Integer(const MetricOptions& options)
    : remote_integer_(internal::RemoteInteger(options)) {}

Integer::Integer(const MetricOptions& options, Collector* collector)
    : remote_integer_(internal::RemoteInteger(options)), collector_(collector) {
  if (collector_ != nullptr) {
    collector_->Subscribe(&remote_integer_.value());
  }
}

Integer::Integer(const MetricOptions& options, internal::FlushInterface** flush_interface)
    : remote_integer_(internal::RemoteInteger(options)) {
  *flush_interface = &remote_integer_.value();
}

Integer::~Integer() {
  if (collector_ != nullptr && remote_integer_.has_value()) {
    collector_->UnSubscribe(&remote_integer_.value());
  }
}

void Integer::Initialize(const MetricOptions& options, Collector* collector) {
  ZX_DEBUG_ASSERT_MSG(!remote_integer_.has_value(), "Cannot renitialize a Integer.");
  collector_ = collector;
  remote_integer_.emplace(options);
  if (collector_ != nullptr) {
    collector_->Subscribe(&remote_integer_.value());
  }
}

void Integer::Set(Integer::Int value) {
  ZX_DEBUG_ASSERT_MSG(remote_integer_.has_value(), "Cannot call |Add| to unintialized Integer.");
  remote_integer_->Exchange(value);
}

Integer::Int Integer::Get() const {
  ZX_DEBUG_ASSERT_MSG(remote_integer_.has_value(),
                      "Cannot call |GetCount| to unintialized Integer.");
  return remote_integer_->Load();
}

}  // namespace cobalt_client
