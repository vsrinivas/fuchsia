// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <cmath>
#include <cstdlib>
#include <limits>
#include <random>

#include <gtest/gtest.h>

#include "lib/media/extend_bits/extend_bits.h"

namespace {

std::random_device random_device;
std::mt19937 twister(random_device());
std::uniform_int_distribution<uint64_t> distribution;

uint64_t RandomUpTo(uint64_t max_value) {
  return distribution(twister, std::uniform_int_distribution<uint64_t>::param_type(0, max_value));
}

}  // namespace

TEST(ExtendBits, Works) {
  const uint32_t kSampleCount = 3000;
  for (uint32_t bits = 2; bits < 63; ++bits) {
    uint64_t modulus = 1ull << bits;
    for (uint32_t i = 0; i < kSampleCount; ++i) {
      uint64_t nearby;
      uint64_t dice_roll = RandomUpTo(6);
      if (dice_roll == 0) {
        nearby = RandomUpTo(modulus);
      } else if (dice_roll == 1) {
        nearby = std::numeric_limits<uint64_t>::max() - RandomUpTo(modulus);
      } else {
        nearby = RandomUpTo(std::numeric_limits<uint64_t>::max());
      }
      uint64_t low_bits = RandomUpTo(modulus - 1);
      uint64_t result = ExtendBits(nearby, low_bits, bits);
      uint64_t nearby_upper_bits = nearby & ~(modulus - 1);
      uint64_t min_distance_so_far = std::numeric_limits<int64_t>::max();
      std::vector<uint64_t> results_so_far;
      for (int64_t sweep = -1; sweep <= 1; ++sweep) {
        // Underflow and overflow are completely ok here.
        uint64_t potential_result = (nearby_upper_bits + (sweep * modulus)) | low_bits;
        uint64_t distance;
        if (potential_result - nearby < nearby - potential_result) {
          distance = potential_result - nearby;
        } else {
          distance = nearby - potential_result;
        }
        if (distance < min_distance_so_far) {
          results_so_far.clear();
        }
        if (distance <= min_distance_so_far) {
          min_distance_so_far = distance;
          results_so_far.emplace_back(potential_result);
        }
      }
      bool reasonable_result_found = false;
      for (const auto& reasonable_result : results_so_far) {
        if (reasonable_result == result) {
          reasonable_result_found = true;
        }
      }
      EXPECT_TRUE(reasonable_result_found);
    }
  }
}

TEST(ExtendBitsGeneral, Works) {
  const uint32_t kSampleCount = 200;
  for (uint64_t modulus = 3; modulus < 1024; ++modulus) {
    for (uint32_t i = 0; i < kSampleCount; ++i) {
      uint64_t nearby;
      uint64_t dice_roll = RandomUpTo(6);
      if (dice_roll == 0) {
        nearby = RandomUpTo(modulus);
      } else if (dice_roll == 1) {
        nearby = std::numeric_limits<uint64_t>::max() - RandomUpTo(modulus);
      } else {
        nearby = RandomUpTo(std::numeric_limits<uint64_t>::max());
      }
      uint64_t before_extension = RandomUpTo(modulus - 1);
      uint64_t result = ExtendBitsGeneral(nearby, before_extension, modulus);
      uint64_t nearby_epoch_index = nearby / modulus;
      uint64_t min_distance_so_far = std::numeric_limits<int64_t>::max();
      std::vector<uint64_t> results_so_far;
      int64_t sweep_start = -1;
      int64_t sweep_end = 1;
      if (nearby < modulus) {
        sweep_start = 0;
      } else {
        uint64_t end_of_the_line_epoch_index = std::numeric_limits<uint64_t>::max() / modulus;
        uint64_t end_of_the_line_non_extended = std::numeric_limits<uint64_t>::max() % modulus;
        if (nearby_epoch_index == end_of_the_line_epoch_index) {
          if (before_extension > end_of_the_line_non_extended) {
            sweep_end = -1;
          } else {
            sweep_end = 0;
          }
        } else if (nearby_epoch_index + 1 == end_of_the_line_epoch_index) {
          if (before_extension > end_of_the_line_non_extended) {
            sweep_end = 0;
          } else {
            sweep_end = 1;
          }
        }
      }
      for (int64_t sweep = sweep_start; sweep <= sweep_end; ++sweep) {
        uint64_t potential_result =
            nearby_epoch_index * modulus + (sweep * modulus) + before_extension;
        uint64_t distance;
        if (potential_result - nearby < nearby - potential_result) {
          distance = potential_result - nearby;
        } else {
          distance = nearby - potential_result;
        }
        if (distance < min_distance_so_far) {
          results_so_far.clear();
        }
        if (distance <= min_distance_so_far) {
          min_distance_so_far = distance;
          results_so_far.emplace_back(potential_result);
        }
      }
      bool reasonable_result_found = false;
      for (const auto& reasonable_result : results_so_far) {
        if (reasonable_result == result) {
          reasonable_result_found = true;
        }
      }
      EXPECT_TRUE(reasonable_result_found);
    }
  }
}
