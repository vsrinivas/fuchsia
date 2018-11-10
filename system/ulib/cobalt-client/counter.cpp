// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string.h>

#include <cobalt-client/cpp/counter-internal.h>
#include <cobalt-client/cpp/counter.h>
#include <zircon/assert.h>

namespace cobalt_client {
namespace internal {

RemoteCounter::RemoteCounter(const RemoteMetricInfo& metric_info)
    : BaseCounter(), metric_info_(metric_info) {
    *buffer_.mutable_event_data() = 0;
}

RemoteCounter::RemoteCounter(RemoteCounter&& other)
    : BaseCounter(fbl::move(other)), buffer_(fbl::move(other.buffer_)),
      metric_info_(other.metric_info_) {}

FlushResult RemoteCounter::Flush(Logger* logger) {
    if (!buffer_.TryBeginFlush()) {
        return FlushResult::kIgnored;
    }
    // Write the current value of the counter to the buffer, and reset it to 0.
    *buffer_.mutable_event_data() = static_cast<uint32_t>(this->Exchange());
    return logger->Log(metric_info_, *buffer_.mutable_event_data()) ? FlushResult::kSucess
                                                                    : FlushResult::kFailed;
}

void RemoteCounter::UndoFlush() {
    this->Increment(*buffer_.mutable_event_data());
}

void RemoteCounter::CompleteFlush() {
    buffer_.CompleteFlush();
}

} // namespace internal

void Counter::Increment(Counter::Count value) {
    remote_counter_.Increment(value);
}

Counter::Count Counter::GetRemoteCount() const {
    return remote_counter_.Load();
}

} // namespace cobalt_client
