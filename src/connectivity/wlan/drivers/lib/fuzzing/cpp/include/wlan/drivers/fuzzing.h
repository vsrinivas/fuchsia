// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_WLAN_DRIVERS_LIB_FUZZING_CPP_INCLUDE_WLAN_DRIVERS_FUZZING_H_
#define SRC_CONNECTIVITY_WLAN_DRIVERS_LIB_FUZZING_CPP_INCLUDE_WLAN_DRIVERS_FUZZING_H_

#include <cstdint>
#include <random>

namespace wlan::drivers::fuzzing {

// Tests should call `randX()` with a UniformRandomBitGenerator, such as the kind returned
// by seeded_rng(), when requiring a random unsigned x-bit integer.
//
// rand8() defined slightly differently since std::uniform_int_distribution requires
// "IntType must be an integer type larger than char".
uint8_t rand8(std::default_random_engine& rng);
static std::uniform_int_distribution<uint16_t> rand16;
static std::uniform_int_distribution<uint32_t> rand32;
static std::uniform_int_distribution<uint64_t> rand64;

// Randomly seeds and returns a `std::default_random_engine`. If `out_seed` is not `nullptr`,
// then `out_seed` is set to the random seed.
std::default_random_engine seeded_rng(std::default_random_engine::result_type* out_seed);

}  // namespace wlan::drivers::fuzzing

#endif  // SRC_CONNECTIVITY_WLAN_DRIVERS_LIB_FUZZING_CPP_INCLUDE_WLAN_DRIVERS_FUZZING_H_
