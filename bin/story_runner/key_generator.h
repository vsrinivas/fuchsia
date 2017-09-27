// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_STORY_RUNNER_KEY_GENERATOR_H_
#define PERIDOT_BIN_STORY_RUNNER_KEY_GENERATOR_H_

#include <string>

#include "lib/fxl/macros.h"

namespace modular {

class TimeOfDay {
 public:
  // Returns number of milliseconds since the epoch
  virtual uint64_t GetTimeOfDayMs() = 0;
};

class WallClockTimeOfDay : public TimeOfDay {
 public:
  // Retursn number of milliseconds since the epoch
  uint64_t GetTimeOfDayMs() override;
};

class RandomNumber {
 public:
  // Returns a random value
  virtual uint64_t RandUint64() = 0;
};

class FuchsiaRandomNumber : public RandomNumber {
 public:
  // Returns a random value
  uint64_t RandUint64() override;
};

// Adapted from the Firebase key generator from:
// https://gist.github.com/mikelehen/3596a30bd69384624c11
class KeyGenerator {
 public:
  KeyGenerator();
  // This ctor is for testing
  KeyGenerator(TimeOfDay* time_of_day, RandomNumber* random_number);
  ~KeyGenerator();

  // Generate a key whose lexicographical order monotonically increases for
  // each call as long as the system clock isn't adjusted backwards.
  std::string Create();

 private:
  uint64_t last_gen_time_{};
  uint64_t last_random_{};

  WallClockTimeOfDay wallclock_time_of_day_;
  FuchsiaRandomNumber fuchsia_random_number_;

  TimeOfDay* const time_of_day_ = &wallclock_time_of_day_;       // Not owned
  RandomNumber* const random_number_ = &fuchsia_random_number_;  // Not owned

  FXL_DISALLOW_COPY_AND_ASSIGN(KeyGenerator);
};

}  // namespace modular

#endif  // PERIDOT_BIN_STORY_RUNNER_KEY_GENERATOR_H_
