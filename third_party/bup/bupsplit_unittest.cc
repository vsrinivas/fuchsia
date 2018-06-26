// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/third_party/bup/bupsplit.h"

#include <stdlib.h>
#include <limits>

#include "gtest/gtest.h"
#include "lib/fxl/logging.h"

namespace bup {
namespace {

class RollSumSplitTest : public ::testing::Test {
 protected:
  void SetUp() override { srand(0); }
};

std::string GetValue(size_t size) {
  std::string value(size, '\0');
  for (size_t i = 0; i < value.size(); ++i) {
    value[i] = rand();
  }
  return value;
}

TEST_F(RollSumSplitTest, CheckMinMax) {
  const size_t min = 4 * 1024;
  const size_t max = 8 * 1024;

  RollSumSplit rh(min, max);

  std::string value = GetValue(1024 * 1024);
  fxl::StringView view = value;
  while (!view.empty()) {
    size_t index = rh.Feed(view, nullptr);
    if (index > 0) {
      EXPECT_TRUE(index >= min);
      EXPECT_TRUE(index <= max);
      view = view.substr(index);
    } else {
      EXPECT_TRUE(view.size() <= max);
      view = "";
    }
  }
}

// Verifies that results are the same when we feed all data at once and when we
// feed the data byte-by-byte.
TEST_F(RollSumSplitTest, CheckSameResult) {
  struct Cut {
    size_t size;
    size_t bits;
  };

  RollSumSplit rh(4 * 1024, 64 * 1024 - 1);

  std::string value = GetValue(1024 * 1024);
  fxl::StringView view = value;
  std::vector<Cut> feed_all_cuts;
  while (!view.empty()) {
    Cut cut;
    cut.size = rh.Feed(view, &cut.bits);
    if (cut.size) {
      view = view.substr(cut.size);
      feed_all_cuts.push_back(cut);
    } else {
      view = "";
    }
  }

  view = value;
  rh.Reset();
  std::vector<Cut> feed_by_byte_cuts;
  size_t index = 0;
  for (size_t i = 0; i < view.size(); ++i) {
    Cut cut;
    size_t count = rh.Feed(view.substr(i, 1), &cut.bits);
    ++index;
    if (count) {
      cut.size = index;
      feed_by_byte_cuts.push_back(cut);
      index = 0;
    }
  }

  ASSERT_EQ(feed_all_cuts.size(), feed_by_byte_cuts.size());
  EXPECT_GT(feed_all_cuts.size(), 0u);
  for (size_t i = 0; i < feed_all_cuts.size(); ++i) {
    EXPECT_EQ(feed_all_cuts[i].size, feed_by_byte_cuts[i].size);
    EXPECT_EQ(feed_all_cuts[i].bits, feed_by_byte_cuts[i].bits);
  }
}

// Check that the roll sum hash only depends on the last |kWindowSize|
// characters.
TEST_F(RollSumSplitTest, CheckWindowed) {
  RollSumSplit r1(0, std::numeric_limits<size_t>::max());
  RollSumSplit r2(0, std::numeric_limits<size_t>::max());

  // Try different initial feeds for the first hasher until finding the case
  // where the 2 hashes do not agree at least once while consuming the
  // |kWindowSize| characters.
  bool finished = false;
  for (size_t initial_feed = 1026; !finished; ++initial_feed) {
    r1.Reset();
    r2.Reset();

    r1.Feed(GetValue(initial_feed), nullptr);

    // Feed kWindowSize characters.
    std::string value = GetValue(kWindowSize);
    fxl::StringView view = value;
    for (size_t i = 0; i < view.size(); ++i) {
      auto f1 = r1.Feed(view.substr(i, 1), nullptr);
      auto f2 = r2.Feed(view.substr(i, 1), nullptr);
      finished = finished || (f1 != f2);
    }
    value = GetValue(1024 * 1024);
    EXPECT_EQ(r1.Feed(value, nullptr), r2.Feed(value, nullptr));
  }
}

}  // namespace
}  // namespace bup
