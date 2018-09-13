// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <threads.h>

#include <cobalt-client/cpp/collector-internal.h>
#include <cobalt-client/cpp/collector.h>
#include <cobalt-client/cpp/counter-internal.h>
#include <cobalt-client/cpp/histogram-internal.h>
#include <cobalt-client/cpp/types-internal.h>
#include <fuchsia/cobalt/c/fidl.h>
#include <lib/fidl/cpp/vector_view.h>
#include <lib/zx/channel.h>

namespace cobalt_client {
namespace {

using internal::Logger;
using internal::Metadata;
using internal::RemoteCounter;
using internal::RemoteHistogram;

Metadata MakeMetadata(uint32_t event_type_index) {
    Metadata metadata;
    metadata.event_type = 0;
    metadata.event_type_index = event_type_index;

    return metadata;
}

} // namespace

Collector::Collector(const CollectorOptions& options, fbl::unique_ptr<internal::Logger> logger)
    : logger_(fbl::move(logger)) {
    flushing_.store(false);
    remote_counters_.reserve(options.max_counters);
    remote_histograms_.reserve(options.max_histograms);
    histogram_options_.reserve(options.max_histograms);
}

Collector::Collector(Collector&& other)
    : histogram_options_(fbl::move(other.histogram_options_)),
      remote_histograms_(fbl::move(other.remote_histograms_)),
      remote_counters_(fbl::move(other.remote_counters_)), logger_(fbl::move(other.logger_)),
      flushing_(other.flushing_.load()) {}

Collector::~Collector() {
    if (logger_ != nullptr) {
        Flush();
    }
};

Histogram Collector::AddHistogram(uint64_t metric_id, uint32_t event_type_index,
                                  const HistogramOptions& options) {
    ZX_DEBUG_ASSERT_MSG(remote_histograms_.size() < remote_histograms_.capacity(),
                        "Exceeded pre-allocated histogram capacity.");
    remote_histograms_.push_back(
        RemoteHistogram(options.bucket_count + 2, metric_id, {MakeMetadata(event_type_index)}));
    histogram_options_.push_back(options);
    size_t index = remote_histograms_.size() - 1;
    return Histogram(&histogram_options_[index], &remote_histograms_[index]);
}

Counter Collector::AddCounter(uint64_t metric_id, uint32_t event_type_index) {
    ZX_DEBUG_ASSERT_MSG(remote_counters_.size() < remote_counters_.capacity(),
                        "Exceeded pre-allocated counter capacity.");
    remote_counters_.push_back(RemoteCounter(metric_id, {MakeMetadata(event_type_index)}));
    size_t index = remote_counters_.size() - 1;
    return Counter(&remote_counters_[index]);
}

void Collector::Flush() {
    // If we are already flushing we just return and do nothing.
    // First come first serve.
    if (flushing_.exchange(true)) {
        return;
    }

    for (auto& histogram : remote_histograms_) {
        LogHistogram(&histogram);
    }

    for (auto& counter : remote_counters_) {
        LogCounter(&counter);
    }

    // Once we are finished we allow flushing again.
    flushing_.store(false);
}

void Collector::LogHistogram(RemoteHistogram* histogram) {
    histogram->Flush([this, histogram](uint64_t metric_id,
                                       const RemoteHistogram::EventBuffer& buffer,
                                       RemoteHistogram::FlushCompleteFn complete_fn) {
        if (!logger_->Log(metric_id, buffer)) {
            // If we failed to log the data, then add the values again to the histogram, so they may
            // be flushed in the future, and we dont need to keep a buffer around for retrying or
            // anything.
            for (auto& bucket : buffer.event_data()) {
                if (bucket.count > 0) {
                    histogram->IncrementCount(bucket.index, bucket.count);
                }
            }
        }

        // Make the buffer writeable again.
        complete_fn();
    });
}

void Collector::LogCounter(RemoteCounter* counter) {
    counter->Flush([this, counter](uint64_t metric_id, const RemoteCounter::EventBuffer& buffer,
                                   RemoteCounter::FlushCompleteFn complete_fn) {
        // Attempt to log data, if we fail, we increase the in process counter by the amount
        // flushed.
        if (!logger_->Log(metric_id, buffer)) {
            if (buffer.event_data() > 0) {
                counter->Increment(buffer.event_data());
            }
        }
        // Make the buffer writeable again.
        complete_fn();
    });
}

} // namespace cobalt_client
