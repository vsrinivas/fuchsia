// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gtest/gtest.h>
#include <wlan/common/element_splitter.h>

#include <vector>

namespace wlan::common {

struct Item {
  uint8_t id;
  Span<const uint8_t> body;
};

static std::vector<Item> RunSplitter(Span<const uint8_t> buffer) {
  std::vector<Item> ret;
  for (auto [id, body] : ElementSplitter(buffer)) {
    ret.push_back(Item{static_cast<uint8_t>(id), body});
  }
  return ret;
}

TEST(ElementSplitter, Empty) { EXPECT_TRUE(RunSplitter({}).empty()); }

TEST(ElementSplitter, LessThanHeader) {
  const uint8_t input[] = {1};
  EXPECT_TRUE(RunSplitter(input).empty());
}

TEST(ElementSplitter, SingleElementWithEmptyBody) {
  const uint8_t input[] = {5, 0};
  auto res = RunSplitter(input);
  ASSERT_EQ(1u, res.size());
  EXPECT_EQ(5u, res[0].id);
  EXPECT_TRUE(res[0].body.empty());
}

TEST(ElementSplitter, SingleElementBufferTooSmall) {
  const uint8_t input[] = {5, 2, 0};
  EXPECT_TRUE(RunSplitter(input).empty());
}

TEST(ElementSplitter, SingleElement) {
  const uint8_t input[] = {5, 2, 0, 0};
  auto res = RunSplitter(input);
  ASSERT_EQ(1u, res.size());
  EXPECT_EQ(5u, res[0].id);
  EXPECT_EQ(&input[2], res[0].body.data());
  EXPECT_EQ(2u, res[0].body.size());
}

TEST(ElementSplitter, SeveralElements) {
  const uint8_t input[] = {5, 2, 0, 0, 6, 0, 7, 1, 0};

  auto res = RunSplitter(input);
  ASSERT_EQ(3u, res.size());

  EXPECT_EQ(&input[2], res[0].body.data());
  EXPECT_EQ(2u, res[0].body.size());

  EXPECT_TRUE(res[1].body.empty());

  EXPECT_EQ(&input[8], res[2].body.data());
  EXPECT_EQ(1u, res[2].body.size());
}

TEST(ElementSplitter, TwoElementsBufferTooSmallForHeader) {
  const uint8_t input[] = {5, 2, 0, 0, 6};
  auto res = RunSplitter(input);
  ASSERT_EQ(1u, res.size());
  EXPECT_EQ(&input[2], res[0].body.data());
  EXPECT_EQ(2u, res[0].body.size());
}

TEST(ElementSplitter, TwoElementsBufferTooSmallForBody) {
  const uint8_t input[] = {5, 2, 0, 0, 6, 3, 0, 0};
  auto res = RunSplitter(input);
  ASSERT_EQ(1u, res.size());
  EXPECT_EQ(&input[2], res[0].body.data());
  EXPECT_EQ(2u, res[0].body.size());
}

}  // namespace wlan::common
