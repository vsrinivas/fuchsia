// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <algorithm>
#include <cstdint>
#include <type_traits>

#include <fbl/algorithm.h>
#include <fbl/string_buffer.h>
#include <fbl/string_printf.h>

namespace fs_metrics {
// This library provides base clases for common attribute types, such as numeric and binary.
// An attribute represents a set of buckets to which a value may be mapped. For example,
// if someone was interested in tracking write latency, the size of the write is important, but
// within certain ranges, so the Attribute provides the mechanisms for mapping the given size to
// the interesting ranges.
//
// Also, the data that is used to store the attribute information must be packed into a struct
// |AttributeData|, so we can rely on type information for inferring the attribute information.
//
// Attributes must provide the following compile time interface:
//
//     Attribute::kSize -> number of different values the given attribute may have.
//
//     Attribute::OffsetOf(ValueType value) -> bucket to which the |value| is mapped to.
//
//     Attribute::ToString(size_t index) -> human readable string of the bucket at |index|
//
//     Attribute::kAttributeValue -> member variable pointer within the data
//                                   struct (AttributeData::*kAttributeValue).

// Attribute for values that can be true or false only.
struct BinaryAttribute {
    // {false, true}
    static constexpr uint64_t kSize = 2;

    // For simplicity bucket 1 is true and bucket 0 is false.
    static constexpr size_t OffsetOf(bool value) { return value ? 1 : 0; }
};

// Attribute for values that map to a range of numbers.
// Attribute class provides:
//     Attribute::kBuckets -> kBuckets[i] : upperbound of bucket[i]
// Attribute ranges do not include the upperbound => [a, b).
template <typename T, typename U>
struct NumericAttribute {
    using Attribute = T;
    using NumericType = U;

    // Compile time check that we have a numeric type.
    static_assert(std::is_integral<NumericType>::value ||
                      std::is_floating_point<NumericType>::value,
                  "Only numeric types are allowed for NumericAttribute.");

    // The number of dimensions is defined by the list of upperbounds provided
    // by the implementing class. An extra overflow bucket is added. The first bucket
    // contains everything from [-inf, upperbound).
    static constexpr uint64_t kSize = fbl::count_of(Attribute::kBuckets) + 1;

    // Performs linear search over an array to find the |Attribute::kBuckets| bucket
    // that is containing the smallest value bigger than |value|.
    static constexpr size_t OffsetOf(NumericType value) {
        static_assert(std::is_same<typename std::decay<decltype(Attribute::kBuckets[0])>::type,
                                   NumericType>::value,
                      "kBuckets type must match NumericType.");
        for (size_t i = 0; i < kSize - 1; ++i) {
            if (value < Attribute::kBuckets[i]) {
                return i;
            }
        }

        return Attribute::kSize - 1;
    }

    // By default numeric attribute are bucketed, and their human readable string is
    // for a bucket = [a,b) => a_b.
    // The first bucket is written as -inf_b and the last bucket(overflow bucket) is written as
    // a_inf.
    static std::string ToString(size_t index) {

        if (index == kSize - 1) {
            return fbl::StringPrintf("%ld_inf", Attribute::kBuckets[kSize - 2]).c_str();
        } else if (index == 0) {
            return fbl::StringPrintf("-inf_%ld", Attribute::kBuckets[0]).c_str();
        }
        return fbl::StringPrintf("%ld_%ld", Attribute::kBuckets[index - 1],
                                 Attribute::kBuckets[index])
            .c_str();
    }
};

template <typename T>
struct NumericAttribute<T, decltype(T::kBuckets[0])>;

} // namespace fs_metrics
