// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <limits>
#include <vector>

#include <lib/fzl/time.h>
#include <lib/inspect-vmo/state.h>
#include <lib/inspect-vmo/types.h>
#include <lib/zx/time.h>

namespace fs_metrics {

// Properties of logged events, which are used internally to find the correct histogram.
struct EventOptions {
    // Matches the block range of an operation.
    int64_t block_count = 0;
    // Used match the depth range of an operation.
    int64_t node_depth = 0;
    // Used to match the node degree range of an operation.
    int64_t node_degree = 0;
    // Used to mark an operation as buffered or cache-hit depending on the context.
    bool buffered = false;
    // Used to mark an operation as successfully completed.
    bool success = false;
};

// List of operations being recorded. Increase |kOperationCount| accordingly when editing this enum.
enum class OperationType {
    kClose,
    kRead,
    kWrite,
    kAppend,
    kTruncate,
    kSetAttr,
    kGetAttr,
    kReadDir,
    kSync,
    kLookUp,
    kCreate,
    kLink,
    kUnlink,
};

// Number of different operation types recorded in this histogram.
constexpr uint64_t kOperationCount = 13;

namespace internal {
// RAII wrapper for keeping track of duration, by calling RecordFn. It's templated on the Clock
// and histogram for ease of testing.
template <typename T, typename V>
class LatencyEventInternal {
public:
    using HistogramCollection = T;
    using Clock = V;

    LatencyEventInternal() = delete;
    explicit LatencyEventInternal(HistogramCollection* histograms, OperationType operation)
        : operation_(operation), histograms_(histograms) {
        Reset();
    }

    LatencyEventInternal(const LatencyEventInternal&) = delete;
    LatencyEventInternal(LatencyEventInternal&&) = default;
    LatencyEventInternal& operator=(const LatencyEventInternal&) = delete;
    LatencyEventInternal& operator=(LatencyEventInternal&&) = delete;
    ~LatencyEventInternal() { Record(); }

    // Explicitly record the latency event, since creation or last call to |LatencyEvent::Reset|
    // until now.
    void Record() {
        if (start_.get() == 0) {
            return;
        }
        histograms_->Record(histograms_->GetHistogramId(operation_, options_),
                            fzl::TicksToNs(Clock::now() - start_));
        Cancel();
    }

    // Resets the start time from the observation, and the event starts tracking again.
    // |options| remain the same.
    void Reset() { start_ = Clock::now(); }

    // Prevents this observation from being recorded.
    void Cancel() { start_ = zx::ticks(0); }

    // Updating the options may change which histogram records this observation.
    EventOptions* mutable_options() { return &options_; }

private:
    EventOptions options_ = {};
    // Records an observation in histograms when LatencyEvent is destroyed or explictly
    // requested to record.
    OperationType operation_;
    HistogramCollection* histograms_ = nullptr;
    zx::ticks start_ = zx::ticks(0);
};
} // namespace internal

// Forward declaration.
class Histograms;

// Alias for exposing the LatencyEvent actually used.
using LatencyEvent = internal::LatencyEventInternal<Histograms, zx::ticks>;

// This class provides a unified view over common metrics collected for file systems.
class Histograms {
public:
    static constexpr char kHistComponent[] = "histograms";

    Histograms() = delete;
    explicit Histograms(inspect::vmo::Object* root);
    Histograms(const Histograms&) = delete;
    Histograms(Histograms&&) = delete;
    Histograms& operator=(const Histograms&) = delete;
    Histograms& operator=(Histograms&&) = delete;
    ~Histograms() = default;

    // Returns a LatencyEvent that will record a latency event for |operation| on destruction unless
    // it is cancelled. |LatencyEvent::mutable_options| allows adjusting the event options.
    LatencyEvent NewLatencyEvent(OperationType operation);

    // Returns a unique Id for a given operation and option set. Depending on the operation,
    // multiple option configurations may be mapped to the same Id. The histogram ids are in the
    // range [0, HistogramCount).
    uint64_t GetHistogramId(OperationType operation, const EventOptions& options) const;

    // Returns the number of different histograms tracking this operation.
    uint64_t GetHistogramCount(OperationType operation);

    // Returns the number of histograms in this collection.
    uint64_t GetHistogramCount() const { return histograms_.size(); }

    // Records |latency| into the histogram mapped to |histogram_id|.
    void Record(uint64_t histogram_id, zx::duration latency);

protected:
    // Nodes of the inspect tree created for the histogram hierarchy.
    std::vector<inspect::vmo::Object> nodes_;

    // Collection of histograms created for each collected metric.
    std::vector<inspect::vmo::ExponentialUintHistogram> histograms_;
};

} // namespace fs_metrics
