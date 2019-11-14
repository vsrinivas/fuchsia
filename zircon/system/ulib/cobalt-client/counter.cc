// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <cobalt-client/cpp/counter.h>

#include <zircon/assert.h>

#include <cstring>
#include <utility>

#include <cobalt-client/cpp/counter-internal.h>

namespace cobalt_client {
namespace internal {

RemoteCounter::RemoteCounter(const MetricInfo& metric_info)
    : BaseCounter(), metric_info_(metric_info) {
  buffer_ = 0;
}

RemoteCounter::RemoteCounter(RemoteCounter&& other)
    : BaseCounter(std::move(other)),
      buffer_(std::move(other.buffer_)),
      metric_info_(other.metric_info_) {}

bool RemoteCounter::Flush(Logger* logger) {
  // Write the current value of the counter to the buffer, and reset it to 0.
  buffer_ = this->Exchange();
  return logger->Log(metric_info_, buffer_);
}

void RemoteCounter::UndoFlush() { this->Increment(buffer_); }

void RemoteCounter::Initialize(const MetricOptions& options) {
  ZX_DEBUG_ASSERT(!options.IsLazy());
  metric_info_ = MetricInfo::From(options);
}

}  // namespace internal

Counter::Counter(const MetricOptions& options)
    : remote_counter_(internal::MetricInfo::From(options)), mode_(options.mode) {
  ZX_DEBUG_ASSERT_MSG(!options.IsLazy(), "Cannot initialize counter with |kLazy| options.");
}

Counter::Counter(const MetricOptions& options, Collector* collector)
    : remote_counter_(internal::MetricInfo::From(options)),
      collector_(collector),
      mode_(options.mode) {
  ZX_DEBUG_ASSERT_MSG(!options.IsLazy(), "Cannot initialize counter with |kLazy| options.");
  if (collector_ != nullptr) {
    collector_->Subscribe(&remote_counter_);
  }
}

Counter::Counter(const MetricOptions& options, internal::FlushInterface** flush_interface)
    : remote_counter_(internal::MetricInfo::From(options)),
      collector_(nullptr),
      mode_(options.mode) {
  ZX_DEBUG_ASSERT_MSG(!options.IsLazy(), "Cannot initialize counter with |kLazy| options.");
  *flush_interface = &remote_counter_;
}

Counter::~Counter() {
  if (collector_ != nullptr) {
    collector_->UnSubscribe(&remote_counter_);
  }
}

void Counter::Initialize(const MetricOptions& options, Collector* collector) {
  ZX_DEBUG_ASSERT_MSG(!options.IsLazy(), "Cannot initialize counter with |kLazy| options.");
  collector_ = collector;
  remote_counter_.Initialize(options);
  mode_ = options.mode;
  if (collector_ != nullptr) {
    collector_->Subscribe(&remote_counter_);
  }
}

void Counter::Increment(Counter::Count value) {
  ZX_DEBUG_ASSERT_MSG(mode_ != MetricOptions::Mode::kLazy,
                      "Cannot operate on metric with mode set to |kLazy|.");
  remote_counter_.Increment(value);
}

Counter::Count Counter::GetRemoteCount() const {
  ZX_DEBUG_ASSERT_MSG(mode_ != MetricOptions::Mode::kLazy,
                      "Cannot operate on metric with mode set to |kLazy|.");
  return remote_counter_.Load();
}

}  // namespace cobalt_client
