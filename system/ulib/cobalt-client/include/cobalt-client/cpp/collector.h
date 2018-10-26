// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stdint.h>
#include <unistd.h>

#include <cobalt-client/cpp/counter.h>
#include <cobalt-client/cpp/histogram.h>
#include <cobalt-client/cpp/metric-options.h>

#include <fbl/atomic.h>
#include <fbl/function.h>
#include <fbl/string.h>
#include <fbl/unique_ptr.h>
#include <fbl/vector.h>
#include <lib/zx/time.h>
#include <lib/zx/vmo.h>

namespace cobalt_client {
namespace internal {
// Forward Declarations.
class RemoteHistogram;
class RemoteCounter;
struct Metadata;
class Logger;
} // namespace internal

// Defines the options for initializing the Collector.
struct CollectorOptions {
    // Callback used when reading the config to create a cobalt logger.
    // Returns true when the write was successful. The VMO will be transferred
    // to the cobalt service.
    fbl::Function<bool(zx::vmo*, size_t*)> load_config;

    // Configuration for RPC behavior for remote metrics.
    // Only set if you plan to interact with cobalt service.

    // When registering with cobalt, will block for this amount of time, each
    // time we need to reach cobalt, until the response is received.
    zx::duration response_deadline;

    // When registering with cobalt, will block for this amount of time, the first
    // time we need to wait for a response.
    zx::duration initial_response_deadline;

    // We need this information for pre-allocating storage
    // and guaranteeing no dangling pointers, plus contiguous
    // memory for cache friendliness.

    // This allows to allocated memory appropiately.

    // Number of histograms to be used.
    size_t max_histograms;

    // Number of counters to be used.
    size_t max_counters;
};

// This class acts as a peer for instantiating Histograms and Counters. All
// objects instantiated through this class act as a view, which means that
// their lifetime is coupled to this object's lifetime. This class does require
// the number of different configurations on construction.
//
// The Sink provides an API for persisting the supported data types. This is
// exposed to simplify testing.
//
// This class is moveable, but not copyable or assignable.
// This class is thread-compatible.
class Collector {
public:
    // Returns a |Collector| whose data will be logged for GA release stage.
    static Collector GeneralAvailability(CollectorOptions options);

    // Returns a |Collector| whose data will be logged for Dogfood release stage.
    static Collector Dogfood(CollectorOptions options);

    // Returns a |Collector| whose data will be logged for Fishfood release stage.
    static Collector Fishfood(CollectorOptions options);

    // Returns a |Collector| whose data will be logged for Debug release stage.
    static Collector Debug(CollectorOptions options);

    Collector(const CollectorOptions& options, fbl::unique_ptr<internal::Logger> logger);
    Collector(const Collector&) = delete;
    Collector(Collector&&);
    Collector& operator=(const Collector&) = delete;
    Collector& operator=(Collector&&) = delete;
    ~Collector();

    // Returns a histogram to log events for a given |metric_id| and |event_type_index|
    // on a histogram described by |options|.
    // Preconditions:
    //     |metric_id| must be greater than 0.
    //     |event_type_index| must be greater than 0.
    Histogram AddHistogram(const HistogramOptions& histogram_options);

    // Returns a counter to log events for a given |metric_id|, |event_type_index| and |component|
    // as a raw counter.
    // Preconditions:
    //     |metric_id| must be greater than 0.
    //     |event_type_index| must be greater than 0.
    // TODO(gevalentino): remove the warning when Cobalt adds the required support.
    Counter AddCounter(const MetricOptions& options);

    // Flushes the content of all flushable metrics into |sink_|. The |sink_| is
    // in charge of persisting the data.
    void Flush();

private:
    void LogHistogram(internal::RemoteHistogram* histogram);
    void LogCounter(internal::RemoteCounter* counter);

    fbl::Vector<HistogramOptions> histogram_options_;
    fbl::Vector<internal::RemoteHistogram> remote_histograms_;
    fbl::Vector<internal::RemoteCounter> remote_counters_;

    fbl::unique_ptr<internal::Logger> logger_;
    fbl::atomic<bool> flushing_;
};

} // namespace cobalt_client
