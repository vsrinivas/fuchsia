// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stdint.h>

#include <fbl/vector.h>
#include <fuchsia/cobalt/c/fidl.h>
#include <lib/fidl/cpp/vector_view.h>

namespace cobalt_client {

// A value pair which represents a bucket index and the count for such index.
using DistributionEntry = fuchsia_cobalt_BucketDistributionEntry;

// Unamed value which represents a single dimension observation.
using Value = fuchsia_cobalt_Value;

// Named value which is a part of a multi dimensional observation.
using ObservationValue = fuchsia_cobalt_ObservationValue;

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

inline Value BucketDistributionValue(size_t size, DistributionEntry* entries) {
    Value val;
    val.tag = fuchsia_cobalt_ValueTagint_bucket_distribution;
    val.int_bucket_distribution.count = size;
    val.int_bucket_distribution.data = entries;
    return val;
}

} // namespace cobalt_client
