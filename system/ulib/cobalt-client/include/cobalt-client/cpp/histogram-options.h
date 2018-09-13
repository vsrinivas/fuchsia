// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stdint.h>

#include <fbl/function.h>
#include <zircon/compiler.h>

namespace cobalt_client {

// Describes an histogram, and provides data for mapping a value to a given bucket.
// Every histogram contains two additional buckets, one at index 0 and bucket_count + 1.
// These buckets are used to store underflow and overflow respectively.
//
// buckets = [-inf, min_value) ...... [max_value, +inf)
//
// Parameters are calculated by the factory methods based on the input parameters,
// so that expectations are met.
//
// If using cobalt to flush your observations to the backend, this options should match
// your metric definitions for correct behavior. Mismatch with the respective metric definition
// will not allow proper collection and aggregation of metrics in the backend.
struct HistogramOptions {
    enum class Type {
        // Each bucket is described in the following form:
        // range(i) =  [ b * i + c, b * {i +1} + c)
        // i = (val - c) / b
        kLinear,
        // Each bucket is described in the following form:
        // range(i) =  [ b * a^i + c, b * a^{i+1} + c)
        // The cost of this type is O(1), because:
        // i = floor(log (val - c)  - log b)/log a
        kExponential,
    };

    // Returns HistogramOptions for a Histogram whose bucket size follow an exponential progression.
    // |scalar| * |base|^(current_step) + |offset| - |scalar| = lowerbound(current_step).
    // offset' = |offset| - |scalar|
    // |scalar| * |base|^(current_step) + offset' = lowerbound(current_step).
    static HistogramOptions Exponential(uint32_t bucket_count, uint32_t base, uint32_t scalar,
                                        int64_t offset);

    // Returns HistogramOptions for a Histogram whose bucket size follow an exponential progression.
    // |scalar| * current_step + offset = lowerbound(current_step).
    static HistogramOptions Linear(uint32_t bucket_count, uint32_t scalar, int64_t offset);

    HistogramOptions() = default;
    HistogramOptions(const HistogramOptions&);

    // Sanity check.
    bool IsValid() const {
        switch (type) {
        case Type::kExponential:
            if (base == 0) {
                return false;
            }
            __FALLTHROUGH;
        case Type::kLinear:
            if (scalar == 0 || bucket_count == 0) {
                return false;
            }
            break;
        }

        return true;
    }

    // This parameters should not be set manually.

    // Function used for mapping a value to a given bucket.
    fbl::Function<uint32_t(double, const HistogramOptions&)> map_fn = nullptr;

    // Function used for mapping a bucket to its lowerbound.
    fbl::Function<double(uint32_t, const HistogramOptions&)> reverse_map_fn = nullptr;

    // Base to describe the width of each step, in |kExponentialWidth|.
    double base = 1;

    // Scalar used by the type. This scales the width of each step.
    double scalar = 1;

    // This matchest offset', which is calculated depending on the histogram type.
    double offset = 0;

    // Number of buckets needed.
    uint32_t bucket_count = 1;

    // Type of the histogram to be constructed.
    Type type;
};

} // namespace cobalt_client
