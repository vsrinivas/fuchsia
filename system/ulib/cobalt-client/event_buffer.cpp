// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <cobalt-client/cpp/types-internal.h>
#include <lib/fidl/cpp/vector_view.h>

namespace cobalt_client {
namespace internal {

template <typename T>
EventBuffer<T>::EventBuffer(const fbl::String& component, const fbl::Vector<Metadata>& metadata)
    : component_(component), flushing_(false), has_component_(true) {
    metadata_.reserve(metadata.size() + 1);
    for (const auto& data : metadata) {
        metadata_.push_back(data);
    }
}

template <typename T>
EventBuffer<T>::EventBuffer(const fbl::Vector<Metadata>& metadata)
    : flushing_(false), has_component_(false) {
    metadata_.reserve(metadata.size() + 1);
    for (const auto& data : metadata) {
        metadata_.push_back(data);
    }
}

template <typename T> EventBuffer<T>::EventBuffer(EventBuffer&& other) {
    flushing_.store(other.flushing_.load());
    buffer_ = fbl::move(other.buffer_);
    metadata_ = fbl::move(other.metadata_);
    component_ = fbl::move(other.component_);
    has_component_ = fbl::move(other.has_component_);
}

template <typename T> EventBuffer<T>::~EventBuffer() = default;

template <typename T> const fidl::StringView EventBuffer<T>::component() const {
    fidl::StringView view;
    view.set_size(component_.size());
    // We use a bit to differentiate between empty string and null string.
    // const_cast is safe, we are returning a constant view.
    view.set_data(has_component_ ? const_cast<char*>(component_.data()) : nullptr);
    return view;
}

// Supported types for cobalt's metric types.
// Counter.
template class EventBuffer<uint32_t>;
// Histogram.
template class EventBuffer<fidl::VectorView<HistogramBucket>>;

} // namespace internal
} // namespace cobalt_client
