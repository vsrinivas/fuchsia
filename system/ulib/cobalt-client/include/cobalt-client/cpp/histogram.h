// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stdint.h>
#include <unistd.h>

#include <cobalt-client/cpp/counter.h>
#include <cobalt-client/cpp/observation.h>
#include <fbl/atomic.h>
#include <fbl/function.h>
#include <fbl/string.h>
#include <fbl/vector.h>

namespace cobalt_client {
namespace internal {
// Base class for histogram, that provide a view to the data and mechanism for
// flushing such data.
//
// This class is thread-safe.
class BaseHistogram {
public:
    // Callback to notify that Flush has been completed, and that the observation buffer is
    // writeable again(this is buffer where the histogram is flushed).
    using FlushCompleteFn = fbl::Function<void()>;

    // Function in charge persisting or processing the ObservationValue buffer.
    using FlushFn = fbl::Function<void(
        uint64_t metric_id, const fidl::VectorView<ObservationValue>&, FlushCompleteFn complete)>;

    BaseHistogram() = delete;
    BaseHistogram(const fbl::String& name, const fbl::Vector<ObservationValue>& metadata,
                  size_t buckets, uint64_t metric_id, uint32_t encoding_id);
    BaseHistogram(const BaseHistogram&) = delete;
    BaseHistogram(BaseHistogram&&);
    BaseHistogram& operator=(const BaseHistogram&) = delete;
    BaseHistogram& operator=(BaseHistogram&&) = delete;
    ~BaseHistogram() = default;

    // Returns true if the contents of histogram were flushed into an ObservationPart collection,
    // which is sent to the flush_handler. Returns false if the call was ignored.
    //
    // VectorView will contain ObservationValue as follows:
    // | Metadata | Histogram|
    bool Flush(const FlushFn& flush_handler);

    // Increases the count of the |bucket| bucket by 1.
    void IncrementCount(uint64_t bucket) {
        ZX_DEBUG_ASSERT_MSG(bucket < buckets_.size(), "Add observation out of range.");
        buckets_[bucket].Increment();
    }

    // Returns the count of the |bucket| bucket.
    Counter::Type GetCount(uint64_t bucket) const {
        ZX_DEBUG_ASSERT_MSG(bucket < buckets_.size(), "Add observation out of range.");
        return buckets_[bucket].Load();
    }

protected:
    fbl::Vector<Counter> buckets_;
    fbl::Vector<ObservationValue> observations_;
    // Buffer for out of line allocation for the data being sent
    // through fidl. This buffer is rewritten on every flush, and contains
    // an entry for each bucket.
    fbl::Vector<DistributionEntry> buffer_;
    fbl::String name_;
    uint64_t metric_id_;
    uint32_t encoding_id_;

    // Enforces that no two flushes can be called concurrently.
    // Only the call the changes the value from false to true, will actually flush.
    fbl::atomic<bool> flushing_;
};

} // namespace internal
} // namespace cobalt_client
