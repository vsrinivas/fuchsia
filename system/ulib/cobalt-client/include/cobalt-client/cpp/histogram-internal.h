// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stdint.h>
#include <unistd.h>

#include <cobalt-client/cpp/counter-internal.h>
#include <cobalt-client/cpp/types-internal.h>
#include <fbl/atomic.h>
#include <fbl/function.h>
#include <fbl/string.h>
#include <fbl/vector.h>

namespace cobalt_client {
namespace internal {
// Note: Everything on this namespace is internal, no external users should rely
// on the behaviour of any of these classes.

// Base class for histogram, that provides a thin layer over a collection of buckets
// that represent a histogram. Once constructed, unless moved, the class is thread-safe.
// All allocations happen when constructed.
//
// This class is moveable but not copyable or assignable.
// This class is thread-compatible.
class BaseHistogram {
public:
    // Type used for histogram samples.
    using Count = BaseCounter::Type;

    BaseHistogram() = delete;
    explicit BaseHistogram(uint32_t num_buckets);
    BaseHistogram(const BaseHistogram&) = delete;
    BaseHistogram(BaseHistogram&&);
    BaseHistogram& operator=(const BaseHistogram&) = delete;
    BaseHistogram& operator=(BaseHistogram&&) = delete;
    ~BaseHistogram() = default;

    // Increases the count of the |bucket| bucket by 1.
    void IncrementCount(uint32_t bucket, Count val = 1) {
        ZX_DEBUG_ASSERT_MSG(bucket < buckets_.size(), "IncrementCount bucket out of range.");
        buckets_[bucket].Increment(val);
    }

    // Returns the count of the |bucket| bucket.
    Count GetCount(uint32_t bucket) const {
        ZX_DEBUG_ASSERT_MSG(bucket < buckets_.size(), "GetCount bucket out of range.");
        return buckets_[bucket].Load();
    }

protected:
    // Counter for the abs frequency of every histogram bucket.
    fbl::Vector<BaseCounter> buckets_;
};

// This class provides a histogram which represents a full fledged cobalt metric. The histogram
// owner will call |Flush| which is meant to incrementally persist data to cobalt.
//
// This class is moveable but not copyable or assignable.
// This class is thread-compatible.
class RemoteHistogram : public BaseHistogram {
public:
    // Callback to notify that Flush has been completed, and that the observation buffer is
    // writeable again(this is buffer where the histogram is flushed).
    using FlushCompleteFn = fbl::Function<void()>;

    // Function in charge persisting or processing the ObservationValue buffer.
    using FlushFn = fbl::Function<void(
        uint64_t metric_id, const fidl::VectorView<ObservationValue>&, FlushCompleteFn complete)>;

    RemoteHistogram() = delete;
    RemoteHistogram(uint32_t num_buckets, const fbl::String& name, uint64_t metric_id,
                    uint32_t encoding_id, const fbl::Vector<ObservationValue>& metadata);
    RemoteHistogram(const RemoteHistogram&) = delete;
    RemoteHistogram(RemoteHistogram&&);
    RemoteHistogram& operator=(const RemoteHistogram&) = delete;
    RemoteHistogram& operator=(RemoteHistogram&&) = delete;
    ~RemoteHistogram() = default;

    // Returns true if the contents of histogram were flushed into an ObservationPart collection,
    // which is sent to the flush_handler. Returns false if the call was ignored.
    //
    // VectorView will contain ObservationValue as follows:
    // | Metadata | Histogram|
    bool Flush(const FlushFn& flush_handler);

private:
    // Keeps a buffer for the metadata and the metric.
    ObservationBuffer buffer_;

    // Buffer for out of line allocation for the data being sent
    // through fidl. This buffer is rewritten on every flush, and contains
    // an entry for each bucket.
    fbl::Vector<BucketDistributionEntry> bucket_buffer_;

    // ObservationPart name for the histogram.
    fbl::String name_;

    // Id for the cobalt metric backed by this histogram.
    uint64_t metric_id_;

    // Id for the encoding used for the histograms data.
    uint32_t encoding_id_;
};

} // namespace internal
} // namespace cobalt_client
