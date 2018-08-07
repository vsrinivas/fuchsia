// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string.h>

#include <cobalt-client/cpp/counter-internal.h>
#include <cobalt-client/cpp/counter.h>

namespace cobalt_client {
namespace internal {

BaseCounter::BaseCounter(BaseCounter&& other) : counter_(other.Exchange(0)) {}

RemoteCounter::RemoteCounter(const fbl::String& name, uint64_t metric_id, uint32_t encoding_id,
                             const fbl::Vector<ObservationValue>& metadata)
    : BaseCounter(), buffer_(metadata), name_(name), metric_id_(metric_id),
      encoding_id_(encoding_id) {
    buffer_.GetMutableMetric()->encoding_id = encoding_id;
    // Add one for the null termination.
    buffer_.GetMutableMetric()->name.size = name_.size() + 1;
    buffer_.GetMutableMetric()->name.data = const_cast<char*>(name_.c_str());
    buffer_.GetMutableMetric()->value = IntValue(0);
}

RemoteCounter::RemoteCounter(RemoteCounter&& other)
    : BaseCounter(fbl::move(other)), buffer_(fbl::move(other.buffer_)), name_(other.name_),
      metric_id_(other.metric_id_), encoding_id_(other.encoding_id_) {}

bool RemoteCounter::Flush(const RemoteCounter::FlushFn& flush_handler) {
    if (!buffer_.TryBeginFlush()) {
        return false;
    }
    // Write the current value of the counter to the buffer, and reset it to 0.
    buffer_.GetMutableMetric()->value.int_value = this->Exchange();
    flush_handler(metric_id_, buffer_.GetView(), [this]() { buffer_.CompleteFlush(); });
    return true;
}
} // namespace internal

Counter::Counter(internal::RemoteCounter* remote_counter) : remote_counter_(remote_counter) {}

void Counter::Increment(Counter::Count value) {
    remote_counter_->Increment(value);
}

Counter::Count Counter::GetRemoteCount() const {
    return remote_counter_->Load();
}

} // namespace cobalt_client
