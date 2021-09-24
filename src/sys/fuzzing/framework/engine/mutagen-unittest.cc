// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/sys/fuzzing/framework/engine/mutagen.h"

#include <unordered_set>

#include <gtest/gtest.h>

namespace fuzzing {
namespace {

constexpr size_t kMaxMutations = 1 << 12;
constexpr size_t kBufSize = 1 << 8;

}  // namespace

class MutagenTest : public ::testing::Test {
 protected:
  void SetUp() override { out_.Reserve(kBufSize); }

  void AddPattern(const char* str) {
    const auto* data = reinterpret_cast<const uint8_t*>(str);
    auto size = strlen(str);
    ASSERT_LT(patterns_.size(), 63U);
    patterns_.emplace_back(Input(std::vector<uint8_t>(data, data + size)));
  }

  void AddPattern(const std::initializer_list<uint8_t>& bytes) {
    ASSERT_LT(patterns_.size(), 63U);
    patterns_.emplace_back(Input(bytes));
  }

  // bool Mutator(Input* out);
  template <typename Mutator>
  void ExpectAllPatterns(Mutator mutator) {
    auto all_found = (1ULL << patterns_.size()) - 1;
    uint64_t found = 0;  // Used as a bitmap.
    for (size_t i = 0; i < kMaxMutations && found != all_found; ++i) {
      out_.Clear();
      if (!mutator(&out_)) {
        continue;
      }
      EXPECT_NE(out_.size(), 0U);
      size_t j = 0;
      for (const auto& pattern : patterns_) {
        if (pattern == out_) {
          found |= (1ULL << j);
          break;
        }
        ++j;
      }
    }
    EXPECT_EQ(found, all_found);
  }

 private:
  Input out_;
  std::vector<Input> patterns_;
};

// Unit tests.

TEST_F(MutagenTest, Mutate) {
  Mutagen mutagen1;
  auto options = DefaultOptions();
  options->set_seed(1);
  mutagen1.Configure(options);

  // Should track mutations.
  Input u = {0, 1, '2', '3'};
  Input v = {4, 5, 6, 7};
  mutagen1.set_input(&u);
  mutagen1.set_crossover(&v);
  EXPECT_EQ(mutagen1.mutations().size(), 0U);

  Input out1;
  out1.Reserve(kBufSize);
  mutagen1.Mutate(&out1);
  EXPECT_EQ(mutagen1.mutations().size(), 1U);

  mutagen1.Mutate(&out1);
  EXPECT_EQ(mutagen1.mutations().size(), 2U);

  mutagen1.set_input(&u);
  EXPECT_EQ(mutagen1.mutations().size(), 0U);

  // Same seed should produce same mutations.
  options->set_seed(1);
  Mutagen mutagen2;
  mutagen1.Configure(options);
  mutagen2.Configure(options);
  mutagen2.set_input(&u);
  mutagen2.set_crossover(&v);
  Input out2;
  out2.Reserve(kBufSize);

  for (size_t i = 0; i < 128; ++i) {
    mutagen1.Mutate(&out1);
    mutagen2.Mutate(&out2);
    EXPECT_EQ(out1, out2);
  }

  // Should have a high probability of using every mutator eventually. This is true for the
  // configured seed and number of mutations.
  const auto& mutations = mutagen1.mutations();
  std::unordered_set<Mutation> unique_mutations(mutations.begin(), mutations.end());
  EXPECT_NE(unique_mutations.count(kSkipSome), 0U);
  EXPECT_NE(unique_mutations.count(kShuffle), 0U);
  EXPECT_NE(unique_mutations.count(kFlip), 0U);
  EXPECT_NE(unique_mutations.count(kReplaceOne), 0U);
  EXPECT_NE(unique_mutations.count(kReplaceUnsigned), 0U);
  EXPECT_NE(unique_mutations.count(kReplaceNum), 0U);
  EXPECT_NE(unique_mutations.count(kReplaceSome), 0U);
  EXPECT_NE(unique_mutations.count(kMergeReplace), 0U);
  EXPECT_NE(unique_mutations.count(kInsertSome), 0U);
  EXPECT_NE(unique_mutations.count(kMergeInsert), 0U);
  EXPECT_NE(unique_mutations.count(kInsertOne), 0U);
  EXPECT_NE(unique_mutations.count(kInsertRepeated), 0U);
}

TEST_F(MutagenTest, SkipSome) {
  Mutagen mutagen;
  mutagen.Configure(DefaultOptions());

  std::vector<uint8_t> v = {0, 1, 2, 3, 4, 5};

  AddPattern({1, 2, 3, 4, 5});
  AddPattern({0, 2, 3, 4, 5});
  AddPattern({0, 1, 3, 4, 5});
  AddPattern({0, 1, 2, 4, 5});
  AddPattern({0, 1, 2, 3, 5});
  AddPattern({0, 1, 2, 3, 4});

  AddPattern({2, 3, 4, 5});
  AddPattern({0, 3, 4, 5});
  AddPattern({0, 1, 4, 5});
  AddPattern({0, 1, 2, 5});
  AddPattern({0, 1, 2, 3});

  AddPattern({3, 4, 5});
  AddPattern({0, 4, 5});
  AddPattern({0, 1, 5});
  AddPattern({0, 1, 2});

  AddPattern({4, 5});
  AddPattern({0, 5});
  AddPattern({0, 1});

  AddPattern({5});
  AddPattern({0});

  ExpectAllPatterns([&](Input* out) { return mutagen.SkipSome(v.data(), v.size(), 5, out); });
}

TEST_F(MutagenTest, Shuffle) {
  Mutagen mutagen;
  mutagen.Configure(DefaultOptions());

  std::vector<uint8_t> v = {0, 1, 2, 3};

  AddPattern({0, 1, 3, 2});
  AddPattern({0, 2, 1, 3});
  AddPattern({0, 2, 3, 1});
  AddPattern({0, 3, 1, 2});
  AddPattern({0, 3, 2, 1});

  AddPattern({1, 0, 2, 3});
  AddPattern({1, 0, 3, 2});
  AddPattern({1, 2, 0, 3});
  AddPattern({1, 2, 3, 0});
  AddPattern({1, 3, 0, 2});
  AddPattern({1, 3, 2, 0});

  AddPattern({2, 0, 1, 3});
  AddPattern({2, 0, 3, 1});
  AddPattern({2, 1, 0, 3});
  AddPattern({2, 1, 3, 0});
  AddPattern({2, 3, 0, 1});
  AddPattern({2, 3, 1, 0});

  AddPattern({3, 0, 1, 2});
  AddPattern({3, 0, 2, 1});
  AddPattern({3, 1, 0, 2});
  AddPattern({3, 1, 2, 0});
  AddPattern({3, 2, 0, 1});
  AddPattern({3, 2, 1, 0});

  ExpectAllPatterns([&](Input* out) { return mutagen.Shuffle(v.data(), v.size(), out); });
}

TEST_F(MutagenTest, Flip) {
  Mutagen mutagen;
  mutagen.Configure(DefaultOptions());

  std::vector<uint8_t> v = {0, 1, 2, 3};

  AddPattern({8, 1, 2, 3});
  AddPattern({4, 1, 2, 3});
  AddPattern({2, 1, 2, 3});
  AddPattern({1, 1, 2, 3});
  AddPattern({0, 9, 2, 3});
  AddPattern({0, 5, 2, 3});
  AddPattern({0, 3, 2, 3});
  AddPattern({0, 0, 2, 3});
  AddPattern({0, 1, 10, 3});
  AddPattern({0, 1, 6, 3});
  AddPattern({0, 1, 0, 3});
  AddPattern({0, 1, 3, 3});
  AddPattern({0, 1, 2, 11});
  AddPattern({0, 1, 2, 7});
  AddPattern({0, 1, 2, 1});
  AddPattern({0, 1, 2, 2});

  ExpectAllPatterns([&](Input* out) { return mutagen.Flip(v.data(), v.size(), out); });
}

TEST_F(MutagenTest, ReplaceOne) {
  Mutagen mutagen;
  mutagen.Configure(DefaultOptions());

  std::vector<uint8_t> v = {0, 1, 2, 3};

  AddPattern({'!', 1, 2, 3});
  AddPattern({0, '&', 2, 3});
  AddPattern({0, 1, '@', 3});
  AddPattern({0, 1, 2, '~'});

  ExpectAllPatterns([&](Input* out) { return mutagen.ReplaceOne(v.data(), v.size(), out); });
}

TEST_F(MutagenTest, ReplaceUnsigned) {
  Mutagen mutagen;
  mutagen.Configure(DefaultOptions());

  std::vector<uint8_t> v = {1, 0, 0, 0, 0, 0, 0, 0};

  // Replaced with size. Leading zeroes for specific bswapped sizes, e.g. uint16_t, etc.
  AddPattern({8, 0, 0, 0, 0, 0, 0, 0});
  AddPattern({0, 8, 0, 0, 0, 0, 0, 0});
  AddPattern({1, 0, 8, 0, 0, 0, 0, 0});
  AddPattern({0, 0, 0, 8, 0, 0, 0, 0});
  AddPattern({1, 0, 0, 0, 8, 0, 0, 0});
  AddPattern({1, 0, 0, 0, 0, 8, 0, 0});
  AddPattern({1, 0, 0, 0, 0, 0, 8, 0});
  AddPattern({0, 0, 0, 0, 0, 0, 0, 8});

  // Add or subtract up to 10.
  AddPattern({15, 0, 0, 0, 0, 0, 0, 0});
  AddPattern({1, 0, 0, 0, 0, 0, 0, 1});
  AddPattern({1, 0, 0xf3, 0xff, 0xff, 0xff, 0, 0});
  AddPattern({0xfd, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff});

  // Add or subtract up to 10 bswapped.
  AddPattern({1, 0, 0, 0, 0, 0, 0xff, 0xf3});
  AddPattern({1, 0xff, 0xff, 0xff, 0xf8, 0, 0, 0});

  // Negate.
  AddPattern({0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff});

  ExpectAllPatterns([&](Input* out) { return mutagen.ReplaceUnsigned(v.data(), v.size(), out); });
}

TEST_F(MutagenTest, ReplaceNum) {
  Mutagen mutagen;
  mutagen.Configure(DefaultOptions());

  std::string s = "a123b";
  const auto* data = reinterpret_cast<const uint8_t*>(s.c_str());

  AddPattern("a421b");
  AddPattern("a221b");
  AddPattern("a160b");
  AddPattern("a642b");

  ExpectAllPatterns([&](Input* out) { return mutagen.ReplaceNum(data, s.size(), out); });
}

TEST_F(MutagenTest, ReplaceSome) {
  Mutagen mutagen;
  mutagen.Configure(DefaultOptions());

  std::vector<uint8_t> v = {0, 1, 2, 3};

  AddPattern({0, 1, 0, 3});
  AddPattern({0, 1, 2, 1});
  AddPattern({2, 1, 2, 3});
  AddPattern({0, 3, 2, 3});
  AddPattern({0, 0, 1, 3});
  AddPattern({0, 1, 0, 1});
  AddPattern({1, 2, 2, 3});
  AddPattern({0, 1, 1, 2});
  AddPattern({2, 3, 2, 3});
  AddPattern({0, 2, 3, 3});
  AddPattern({0, 0, 1, 2});
  AddPattern({1, 2, 3, 3});

  ExpectAllPatterns([&](Input* out) { return mutagen.ReplaceSome(v.data(), v.size(), out); });
}

TEST_F(MutagenTest, MergeReplace) {
  Mutagen mutagen;
  mutagen.Configure(DefaultOptions());

  std::vector<uint8_t> u = {0, 1, 2, 3};
  std::vector<uint8_t> v = {4, 5, 6, 7};

  AddPattern({0, 1, 2, 3});
  AddPattern({0, 1, 2, 7});
  AddPattern({0, 1, 6, 7});
  AddPattern({0, 5, 6, 7});
  AddPattern({4, 5, 6, 7});

  AddPattern({0, 5, 6, 3});
  AddPattern({4, 1, 2, 7});

  ExpectAllPatterns([&](Input* out) {
    return mutagen.MergeReplace(u.data(), u.size(), v.data(), v.size(), out);
  });
}

TEST_F(MutagenTest, InsertSome) {
  Mutagen mutagen;
  mutagen.Configure(DefaultOptions());

  std::vector<uint8_t> v = {0, 1, 2, 3};

  AddPattern({0, 1, 2, 0, 3});
  AddPattern({0, 1, 1, 2, 3});
  AddPattern({0, 2, 1, 2, 3});

  AddPattern({0, 1, 2, 2, 3, 3});
  AddPattern({0, 1, 1, 2, 2, 3});
  AddPattern({0, 0, 1, 1, 2, 3});

  AddPattern({0, 1, 2, 3, 0, 1, 2, 3});

  ExpectAllPatterns([&](Input* out) { return mutagen.InsertSome(v.data(), v.size(), 8, out); });
}

TEST_F(MutagenTest, MergeInsert) {
  Mutagen mutagen;
  mutagen.Configure(DefaultOptions());

  std::vector<uint8_t> u = {0, 1, 2, 3};
  std::vector<uint8_t> v = {4, 5, 6, 7};

  AddPattern({0, 1, 2, 3, 4, 5, 6, 7});
  AddPattern({0, 1, 2, 4, 3, 5, 6, 7});
  AddPattern({0, 1, 4, 5, 2, 3, 6, 7});
  AddPattern({0, 4, 5, 6, 1, 2, 3, 7});
  AddPattern({4, 5, 6, 7, 0, 1, 2, 3});

  ExpectAllPatterns([&](Input* out) {
    return mutagen.MergeInsert(u.data(), u.size(), v.data(), v.size(), 8, out);
  });
}

TEST_F(MutagenTest, InsertOne) {
  Mutagen mutagen;
  mutagen.Configure(DefaultOptions());

  std::vector<uint8_t> v = {0, 1, 2, 3};

  AddPattern({'!', 0, 1, 2, 3});
  AddPattern({0, '&', 1, 2, 3});
  AddPattern({0, 1, ';', 2, 3});
  AddPattern({0, 1, 2, '@', 3});
  AddPattern({0, 1, 2, 3, '~'});

  ExpectAllPatterns([&](Input* out) { return mutagen.InsertOne(v.data(), v.size(), out); });
}

TEST_F(MutagenTest, InsertRepeated) {
  Mutagen mutagen;
  mutagen.Configure(DefaultOptions());

  std::vector<uint8_t> v = {0, 1, 2, 3};

  AddPattern({0xff, 0xff, 0xff, 0, 1, 2, 3});
  AddPattern({0, 1, 'f', 'f', 'f', 2, 3});
  AddPattern({0, 1, 2, 3, 0, 0, 0});

  ExpectAllPatterns([&](Input* out) { return mutagen.InsertRepeated(v.data(), v.size(), 7, out); });
}

}  // namespace fuzzing
