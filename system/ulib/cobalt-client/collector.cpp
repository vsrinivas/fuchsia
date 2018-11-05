// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <threads.h>

#include <cobalt-client/cpp/collector-internal.h>
#include <cobalt-client/cpp/collector.h>
#include <cobalt-client/cpp/counter-internal.h>
#include <cobalt-client/cpp/histogram-internal.h>
#include <cobalt-client/cpp/metric-options.h>
#include <cobalt-client/cpp/types-internal.h>

#include <fuchsia/cobalt/c/fidl.h>
#include <lib/fdio/util.h>
#include <lib/fidl/cpp/vector_view.h>
#include <lib/zx/channel.h>

namespace cobalt_client {
namespace {

using internal::Logger;
using internal::Metadata;
using internal::RemoteCounter;
using internal::RemoteHistogram;

// Types for metrics
constexpr uint8_t kLocal = 0x1;
constexpr uint8_t kRemote = 0x2;

Collector MakeCollector(CollectorOptions options, internal::CobaltOptions cobalt_options) {
    ZX_DEBUG_ASSERT_MSG(options.load_config, "Must define a load_config function.");
    cobalt_options.logger_deadline_first_attempt = options.initial_response_deadline;
    cobalt_options.logger_deadline = options.response_deadline;
    cobalt_options.config_reader = fbl::move(options.load_config);
    cobalt_options.service_connect = [](const char* service_path,
                                        zx::channel service) -> zx_status_t {
        return fdio_service_connect(service_path, service.release());
    };
    cobalt_options.service_path.AppendPrintf("/svc/%s", fuchsia_cobalt_LoggerFactory_Name);
    return Collector(options, fbl::make_unique<internal::CobaltLogger>(fbl::move(cobalt_options)));
}

} // namespace

void MetricOptions::Local() {
    type = kLocal;
}

void MetricOptions::Remote() {
    type = kRemote;
}

void MetricOptions::Both() {
    type = kLocal | kRemote;
}

bool MetricOptions::IsLocal() const {
    return (type & kLocal) != 0;
}

bool MetricOptions::IsRemote() const {
    return (type & kRemote) != 0;
}

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

Histogram Collector::AddHistogram(const HistogramOptions& options) {
    ZX_DEBUG_ASSERT_MSG(remote_histograms_.size() < remote_histograms_.capacity(),
                        "Exceeded pre-allocated histogram capacity.");
    RemoteHistogram::EventBuffer buffer;
    // RemoteMetricInfo metric_info
    remote_histograms_.push_back(RemoteHistogram(
        options.bucket_count + 2, internal::RemoteMetricInfo::From(options), fbl::move(buffer)));
    histogram_options_.push_back(options);
    size_t index = remote_histograms_.size() - 1;
    return Histogram(&histogram_options_[index], &remote_histograms_[index]);
}

Counter Collector::AddCounter(const MetricOptions& options) {
    ZX_DEBUG_ASSERT_MSG(remote_counters_.size() < remote_counters_.capacity(),
                        "Exceeded pre-allocated counter capacity.");
    RemoteCounter::EventBuffer buffer;
    remote_counters_.push_back(
        RemoteCounter(internal::RemoteMetricInfo::From(options), fbl::move(buffer)));
    size_t index = remote_counters_.size() - 1;
    return Counter(&(remote_counters_[index]));
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
    histogram->Flush([this, histogram](const internal::RemoteMetricInfo& metric_info,
                                       const internal::RemoteHistogram::EventBuffer& buffer,
                                       RemoteHistogram::FlushCompleteFn complete_fn) {
        if (!logger_->Log(metric_info, buffer)) {
            // If we failed to log the data, then add the values again to the histogram, so they may
            // be flushed in the future, and we don't need to keep a buffer around for retrying or
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
    counter->Flush([this, counter](const internal::RemoteMetricInfo& metric_info,
                                   const internal::RemoteCounter::EventBuffer& buffer,
                                   RemoteCounter::FlushCompleteFn complete_fn) {
        // Attempt to log data, if we fail, we increase the in process counter by the amount
        // flushed.
        if (!logger_->Log(metric_info, buffer)) {
            if (buffer.event_data() > 0) {
                counter->Increment(buffer.event_data());
            }
        }
        // Make the buffer writeable again.
        complete_fn();
    });
}

Collector Collector::GeneralAvailability(CollectorOptions options) {
    internal::CobaltOptions cobalt_options;
    cobalt_options.release_stage = internal::ReleaseStage::kGa;
    return MakeCollector(fbl::move(options), fbl::move(cobalt_options));
}

Collector Collector::Dogfood(CollectorOptions options) {
    internal::CobaltOptions cobalt_options;
    cobalt_options.release_stage = internal::ReleaseStage::kDogfood;
    return MakeCollector(fbl::move(options), fbl::move(cobalt_options));
}

Collector Collector::Fishfood(CollectorOptions options) {
    internal::CobaltOptions cobalt_options;
    cobalt_options.release_stage = internal::ReleaseStage::kFishfood;
    return MakeCollector(fbl::move(options), fbl::move(cobalt_options));
}

Collector Collector::Debug(CollectorOptions options) {
    internal::CobaltOptions cobalt_options;
    cobalt_options.release_stage = internal::ReleaseStage::kDebug;
    return MakeCollector(fbl::move(options), fbl::move(cobalt_options));
}

} // namespace cobalt_client
