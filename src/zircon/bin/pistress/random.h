// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_ZIRCON_BIN_PISTRESS_RANDOM_H_
#define SRC_ZIRCON_BIN_PISTRESS_RANDOM_H_

#include <algorithm>
#include <random>

class Random {
 public:
  static inline bool RollDice(float action_prob) {
    return ((action_prob >= 1.0) || ((action_prob > 0.0) && (normal_dist_(rng_) < action_prob)));
  }

  template <typename T>
  static inline T Get(std::uniform_int_distribution<T>& dist) {
    return dist(rng_);
  }

  template <typename Collection>
  static inline void Shuffle(Collection& c) {
    std::shuffle(c.begin(), c.end(), rng_);
  }

 private:
  static inline std::default_random_engine rng_{0x12345678};
  static inline std::uniform_real_distribution<float> normal_dist_{0.0, 1.0};
};

#endif  // SRC_ZIRCON_BIN_PISTRESS_RANDOM_H_
