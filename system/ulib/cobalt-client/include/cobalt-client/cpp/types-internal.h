// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stdint.h>
#include <unistd.h>

#include <cobalt-client/cpp/metric-options.h>

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

struct RemoteMetricInfo {
    // Generates |name| from the contents of metric options.
    static RemoteMetricInfo From(const MetricOptions& options);

    RemoteMetricInfo() = default;
    RemoteMetricInfo(const RemoteMetricInfo&) = default;

    // Allows comparing two |RemoteMetricInfo|, which is a shortcut for checking if
    // all fields are equal.
    bool operator==(const RemoteMetricInfo& rhs) const;
    bool operator!=(const RemoteMetricInfo& rhs) const;

    // Provides refined metric collection for remote metrics.
    // Warning: |component| is not yet supported in the backend, so it will be ignored.
    fbl::String component;

    // Used by remote metrics to match with the respective unique id for the projects defined
    // metrics in the backend.
    uint32_t metric_id;

    // Provides refined metric collection for remote metrics.
    // Warning: |event_code| is not yet supported in the backend, so it will be treated as 0.
    uint32_t event_code;
};

struct LocalMetricInfo {
    // Generates |name| from the contents of metric options.
    static LocalMetricInfo From(const MetricOptions& options);

    LocalMetricInfo() = default;
    LocalMetricInfo(const LocalMetricInfo&) = default;
    bool operator==(const LocalMetricInfo& rhs) const;
    bool operator!=(const LocalMetricInfo& rhs) const;

    fbl::String name;
};

// Wraps a collection of observations. The buffer provides two methods for
// flushing the buffer. Flushing the buffer is an operation were the contents
// are being transferred, during this transfer the buffer becomes unwriteable
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
// This class is thread-compatible, and thread-safe if a thread only access the buffer data,
// when TryBeginFlush is true.
// This class is moveable, but not copyable or assignable.
template <typename BufferType>
class EventBuffer {
public:
    EventBuffer() : flushing_(false) {}
    EventBuffer(const EventBuffer&) = delete;
    EventBuffer(EventBuffer&&);
    EventBuffer& operator=(const EventBuffer&) = delete;
    EventBuffer& operator=(EventBuffer&&) = delete;
    ~EventBuffer();

    const BufferType& event_data() const { return buffer_; }

    // Returns a pointer to metric where the value should be written.
    // The metric should only be modified by a flushing thread, and only during the flushing
    // operation.
    BufferType* mutable_event_data() { return &buffer_; }

    // Returns true if the calling thread successfully started a flush. Only a single thread
    // at any point can start a flush, and once started, no flush can start until
    // the started flush is completed.
    bool TryBeginFlush() { return !flushing_.exchange(true); }

    // Makes the buffer writable again, by marking the flushing operation as complete.
    void CompleteFlush() { flushing_.exchange(false); };

private:
    // Dumping ground for the metric itself for recording.
    BufferType buffer_;

    fbl::atomic<bool> flushing_;
};

} // namespace internal
} // namespace cobalt_client
