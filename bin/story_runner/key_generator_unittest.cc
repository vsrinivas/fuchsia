// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/modular/src/story_runner/key_generator.h"

#include <memory>
#include <utility>

#include "gtest/gtest.h"
#include "lib/fxl/logging.h"

namespace modular {
namespace {

class MockTimeOfDay : public TimeOfDay {
 public:
  // This is just a seed random value from when the code was written
  // so that the mock will generate consistent results.
  explicit MockTimeOfDay(uint64_t value = 1503100825u * 1000) : value_(value) {}

  // Return number of milliseconds since the epoch
  uint64_t GetTimeOfDayMs() override { return value_; }
  void Increment() { ++value_; }

  uint64_t value_;
};

// Generate up to four numbers using pregenerated random values.
class MockRandomNumber : public RandomNumber {
 public:
  uint64_t RandUint64() override {
    FXL_CHECK(call_count_ != values_.size());
    return values_[call_count_++];
  }

  void SetVector(std::vector<uint64_t> values) { values_ = std::move(values); }

  int call_count() { return call_count_; }

 private:
  // This are pregenerated random values. No special meaning.
  std::vector<uint64_t> values_ = {0xb44ca87bb37ba594, 0xc7d582eb78726fc6,
                                   0x32742b5492aa2b71, 0xf11385fa57b130ee};
  size_t call_count_{};
};

TEST(KeyGeneratorTest, Simple_Success) {
  MockTimeOfDay tod;
  MockRandomNumber rng;
  KeyGenerator gen(&tod, &rng);

  EXPECT_EQ("--2rmaqcGBe6inTuLJ", gen.Create());
}

TEST(KeyGeneratorTest, NoMocks_Success) {
  KeyGenerator gen;

  auto t1 = gen.Create();
  auto t2 = gen.Create();
  EXPECT_LT(t1, t2);

  WallClockTimeOfDay time_of_day;
  auto tick = time_of_day.GetTimeOfDayMs();
  while (tick == time_of_day.GetTimeOfDayMs()) {
  }

  auto t3 = gen.Create();
  EXPECT_LT(t2, t3);
}

TEST(KeyGeneratorTest, RngOverflow_Success) {
  MockTimeOfDay tod;
  MockRandomNumber rng;
  rng.SetVector(std::vector<uint64_t>({0x32742b5492aa2bff}));
  KeyGenerator gen(&tod, &rng);
  auto t1 = gen.Create();
  EXPECT_EQ(1, rng.call_count());
  auto t2 = gen.Create();
  EXPECT_EQ(1, rng.call_count());
  EXPECT_LT(t1, t2);
}

// If we ask for a key but the clock hasn't changed, then the keys should
// still be ordered.
TEST(KeyGeneratorTest, NoTickOrdering_Success) {
  MockTimeOfDay tod;
  MockRandomNumber rng;
  KeyGenerator gen(&tod, &rng);
  auto t1 = gen.Create();
  EXPECT_EQ(1, rng.call_count());
  auto t2 = gen.Create();
  EXPECT_EQ(1, rng.call_count());
  EXPECT_LT(t1, t2);

  auto t3 = gen.Create();
  EXPECT_EQ(1, rng.call_count());
  EXPECT_LT(t2, t3);
}

TEST(KeyGeneratorTest, TickOrdering_Success) {
  MockTimeOfDay tod;
  MockRandomNumber rng;
  KeyGenerator gen(&tod, &rng);
  auto t1 = gen.Create();

  tod.Increment();
  auto t2 = gen.Create();

  EXPECT_LT(t1, t2);

  tod.Increment();
  auto t3 = gen.Create();

  EXPECT_LT(t2, t3);
}

}  // namespace
}  // namespace modular
