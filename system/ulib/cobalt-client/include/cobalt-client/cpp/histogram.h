// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <cobalt-client/cpp/metric-options.h>
#include <stdint.h>

namespace cobalt_client {
namespace internal {
// Forward Declaration
class RemoteHistogram;
} // namespace internal

// Thin wrapper for a histogram. This class does not own the data, but acts as a proxy.
//
// This class is copyable, moveable and assignable.
// This class is thread-safe.
class Histogram {
public:
    // Underlying type used for representing bucket count.
    using Count = uint64_t;

    Histogram() = delete;
    Histogram(HistogramOptions* options, internal::RemoteHistogram* remote_histogram);
    Histogram(const Histogram&);
    Histogram(Histogram&&);
    Histogram& operator=(const Histogram&);
    Histogram& operator=(Histogram&&);
    ~Histogram();

    // Increases the count of the bucket containing |value| by |times|.
    // |ValueType| must either be an (u)int or a double.
    template <typename ValueType>
    void Add(ValueType value, Count times = 1);

    // Returns the count of the bucket containing |value|, since it was last sent
    // to cobalt.
    // |ValueType| must either be an (u)int or a double.
    template <typename ValueType>
    Count GetRemoteCount(ValueType value) const;

private:
    // Set of options that define this histogram.
    HistogramOptions* options_;

    // Implementation of the flushable histogram. The value
    // of this histogram is flushed by the collector.
    internal::RemoteHistogram* remote_histogram_;
};
} // namespace cobalt_client
