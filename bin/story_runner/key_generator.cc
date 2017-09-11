// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/modular/src/story_runner/key_generator.h"

#include <sys/time.h>
#include <string>
#include <vector>

#include "lib/fxl/logging.h"
#include "lib/fxl/random/rand.h"

namespace {

constexpr char kEncodingDictionary[] =
    "-0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ_abcdefghijklmnopqrstuvwxyz";

}  // namespace

namespace modular {

uint64_t WallClockTimeOfDay::GetTimeOfDayMs() {
  struct timeval te;
  gettimeofday(&te, nullptr);
  auto milliseconds =
      static_cast<uint64_t>(te.tv_sec) * 1000LL + te.tv_usec / 1000;
  return milliseconds;
}

uint64_t FuchsiaRandomNumber::RandUint64() {
  return fxl::RandUint64();
}

KeyGenerator::KeyGenerator() = default;

KeyGenerator::KeyGenerator(TimeOfDay* time_of_day, RandomNumber* random_number)
    : time_of_day_(time_of_day), random_number_(random_number) {}

KeyGenerator::~KeyGenerator() = default;

std::string KeyGenerator::Create() {
  uint64_t milliseconds = time_of_day_->GetTimeOfDayMs();
  if (milliseconds == last_gen_time_) {
    // Collision with last generated value. We can't simply create a new
    // random number because the lexical ordering would be wrong.
    ++last_random_;
  } else {
    last_random_ = random_number_->RandUint64();
    last_gen_time_ = milliseconds;
  }

  std::string id(18, '-');
  for (int i = 7; i >= 0; --i) {
    id[i] = kEncodingDictionary[static_cast<size_t>(milliseconds % 64)];
    milliseconds /= 64;
  }
  FXL_DCHECK(milliseconds == 0);

  auto last_random = last_random_;

  // The random number must be encoded with lowest bits at the end because
  // we increment the rng above and that must be ordered properly.
  // TODO(jimbe) We are only using 60 bits of randomness. Not enough for
  // production, but enough for the moment.
  for (int i = 17; i >= 8; --i) {
    id[i] = kEncodingDictionary[last_random % 64];
    last_random /= 64;
  }

  return id;
}

}  // namespace modular
