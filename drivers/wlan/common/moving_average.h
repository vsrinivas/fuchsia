// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <limits>
#include <type_traits>

namespace wlan {
namespace common {

template <typename ValueType, typename SumType, uint32_t N> class MovingAverage {
    static_assert(std::numeric_limits<ValueType>::max() * N < std::numeric_limits<SumType>::max(),
                  "'SumType' too small and cannot hold the maximum sum.");
    static_assert(std::is_arithmetic<ValueType>::value, "'ValueType' must be numeric.");
    static_assert(std::is_arithmetic<SumType>::value, "'SumType' must be numeric.");

   public:
    ValueType avg() { return (n == 0 ? 0 : sum / n); }

    void add(ValueType item) {
        if (n < N) {
            n++;
        } else {
            sum -= items[i];
        }
        sum += item;
        items[i] = item;
        i = ++i % N;
    }

    void reset() { i = n = sum = 0; }

   private:
    ValueType items[N];
    SumType sum = 0;
    uint32_t i = 0;
    uint32_t n = 0;
};

}  // namespace common
}  // namespace wlan
