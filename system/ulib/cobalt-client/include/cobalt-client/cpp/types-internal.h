// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stdint.h>
#include <unistd.h>

#include <fbl/atomic.h>
#include <fbl/vector.h>
#include <fuchsia/cobalt/c/fidl.h>
#include <lib/fidl/cpp/vector_view.h>

namespace cobalt_client {
namespace internal {
// Note: Everything on this namespace is internal, no external users should rely
// on the behaviour of any of these classes.

// A value pair which represents a bucket index and the count for such index.
using BucketDistributionEntry = fuchsia_cobalt_BucketDistributionEntry;

// Unamed value which represents a single dimension observation.
using Value = fuchsia_cobalt_Value;

// Named value which is a part of a multi dimensional observation.
using ObservationValue = fuchsia_cobalt_ObservationValue;

// The following functions present a convenient way for initializing
// FIDL union for a given value type. The functions intentionally
// allocates the memory for Value in the stack, to enable (Named) Return value
// optimization.
inline Value IntValue(uint64_t value) {
    Value val;
    val.tag = fuchsia_cobalt_ValueTagint_value;
    val.int_value = value;
    return val;
}

inline Value DoubleValue(double value) {
    Value val;
    val.tag = fuchsia_cobalt_ValueTagdouble_value;
    val.double_value = value;
    return val;
}

inline Value IndexValue(uint32_t value) {
    Value val;
    val.tag = fuchsia_cobalt_ValueTagindex_value;
    val.index_value = value;
    return val;
}

inline Value BucketDistributionValue(size_t size, BucketDistributionEntry* entries) {
    Value val;
    val.tag = fuchsia_cobalt_ValueTagint_bucket_distribution;
    val.int_bucket_distribution.count = size;
    val.int_bucket_distribution.data = entries;
    return val;
}

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
class ObservationBuffer {
public:
    ObservationBuffer() = delete;
    ObservationBuffer(const fbl::Vector<ObservationValue>& metadata);
    ObservationBuffer(const ObservationBuffer&) = delete;
    ObservationBuffer(ObservationBuffer&&);
    ObservationBuffer& operator=(const ObservationBuffer&) = delete;
    ObservationBuffer& operator=(ObservationBuffer&&) = delete;
    ~ObservationBuffer();

    // Returns a pointer to metric where the value should be written.
    // The metric should only be modified by a flushing thread, and only during the flushing
    // operation.
    ObservationValue* GetMutableMetric() { return &buffer_[buffer_.size() - 1]; }

    // Returns a view of the underlying data.
    const fidl::VectorView<ObservationValue> GetView() const;

    // Returns true if the calling thread succesfully started a flush. Only a single thread
    // at any point can start a flush, and once started, no flush can start until
    // the started flush is completed.
    bool TryBeginFlush() {
        return !flushing_.exchange(true);
    }

    // Makes the buffer writable again, by marking the flushing operation as complete.
    void CompleteFlush() { flushing_.exchange(false); };

private:
    fbl::Vector<ObservationValue> buffer_;

    fbl::atomic<bool> flushing_;
};

} // namespace internal
} // namespace cobalt_client
