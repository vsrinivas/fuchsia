// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stdint.h>
#include <unistd.h>

#include <fbl/atomic.h>
#include <fbl/string.h>
#include <fbl/vector.h>
#include <fuchsia/cobalt/c/fidl.h>
#include <lib/fidl/cpp/string_view.h>

namespace cobalt_client {
namespace internal {
// Note: Everything on this namespace is internal, no external users should rely
// on the behaviour of any of these classes.

// A value pair which represents a bucket index and the count for such index.
using HistogramBucket = fuchsia_cobalt_HistogramBucket;

enum class ReleaseStage : fuchsia_cobalt_ReleaseStage {
    kGa = fuchsia_cobalt_ReleaseStage_GA,
    kDogfood = fuchsia_cobalt_ReleaseStage_DOGFOOD,
    kFishfood = fuchsia_cobalt_ReleaseStage_FISHFOOD,
    kDebug = fuchsia_cobalt_ReleaseStage_DEBUG,
};

// Each metadata entry is defined as a pair describing a dimension and
// the value of the given dimension. This values are defined in the metric
// definition.
struct Metadata {
    uint32_t event_type;
    uint32_t event_type_index;
};

// Wraps a collection of observations. The buffer provides two methods for
// flushing the buffer. Flushing the buffer is an operation were the contents
// are being transfered, during this transfer the buffer becomes unwriteable
// until the flush is marked as complete. Any synchronization is left to the
// user, but |TryBeginFlush| will return true for exactly one thread in a
// concurrent environment, it is the job of the user to notify when the
// transfer is complete.
//
// if (!buffer_.TryBeginFlush()) {
//    return;
// }
// // Do Flush.
// buffer_.CompleteFlush();
//
// Note: To make the behaviour more predictable and easier to verify,
// the metadata will always come before the metric, and metric will always be
// the last element in the buffer.
//
// This class is thread-compatible.
// This class is moveable, but not copyable or assignable.
template <typename BufferType> class EventBuffer {
public:
    EventBuffer() = delete;
    explicit EventBuffer(const fbl::Vector<Metadata>& metadata);
    EventBuffer(const fbl::String& component, const fbl::Vector<Metadata>& metadata);
    EventBuffer(const EventBuffer&) = delete;
    EventBuffer(EventBuffer&&);
    EventBuffer& operator=(const EventBuffer&) = delete;
    EventBuffer& operator=(EventBuffer&&) = delete;
    ~EventBuffer();

    const fbl::Vector<Metadata>& metadata() const { return metadata_; }

    const BufferType& event_data() const { return buffer_; }

    const fidl::StringView component() const;

    // Returns a pointer to metric where the value should be written.
    // The metric should only be modified by a flushing thread, and only during the flushing
    // operation.
    BufferType* mutable_event_data() { return &buffer_; }

    // Returns true if the calling thread succesfully started a flush. Only a single thread
    // at any point can start a flush, and once started, no flush can start until
    // the started flush is completed.
    bool TryBeginFlush() { return !flushing_.exchange(true); }

    // Makes the buffer writable again, by marking the flushing operation as complete.
    void CompleteFlush() { flushing_.exchange(false); };

private:
    // Unique string representing a component.
    fbl::String component_;

    // Collection of metadata for the given metric.
    fbl::Vector<Metadata> metadata_;

    // Dumping ground for the metric itself for recording.
    BufferType buffer_;

    fbl::atomic<bool> flushing_;

    bool has_component_;
};

} // namespace internal
} // namespace cobalt_client
