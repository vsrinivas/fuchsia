// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <cobalt-client/cpp/types-internal.h>

namespace cobalt_client {
namespace internal {

ObservationBuffer::ObservationBuffer(const fbl::Vector<ObservationValue>& metadata)
    : flushing_(false) {
    buffer_.reserve(metadata.size() + 1);
    for (const auto& data : metadata) {
        buffer_.push_back(data);
    }
    buffer_.push_back({});
}

ObservationBuffer::ObservationBuffer(ObservationBuffer&& other) {
    flushing_.store(other.flushing_.load());
    buffer_ = fbl::move(other.buffer_);
}

ObservationBuffer::~ObservationBuffer() = default;

// Returns a view of the underlying data.
const fidl::VectorView<ObservationValue> ObservationBuffer::GetView() const {
    fidl::VectorView<ObservationValue> view;
    view.set_data(buffer_.get());
    view.set_count(buffer_.size());
    return view;
}

} // namespace internal
} // namespace cobalt_client
