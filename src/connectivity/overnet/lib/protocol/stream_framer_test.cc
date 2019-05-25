// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gtest/gtest.h>

#include "src/connectivity/overnet/lib/environment/trace.h"
#include "src/connectivity/overnet/lib/protocol/reliable_framer.h"
#include "src/connectivity/overnet/lib/protocol/unreliable_framer.h"

namespace overnet {
namespace stream_framer_test {

struct TestArg {
  std::function<std::unique_ptr<StreamFramer>()> make_framer;
  Slice enframe;
};

struct StreamFramerTest : public ::testing::TestWithParam<TestArg> {};

TEST_P(StreamFramerTest, DeframesFramed) {
  ScopedSeverity severity(Severity::ERROR);
  auto framer = GetParam().make_framer();
  const auto enframe = GetParam().enframe;
  const auto framed = framer->Frame(enframe);
  framer->Push(framed);
  auto deframed = framer->Pop();
  ASSERT_TRUE(deframed.is_ok()) << deframed << " framed=" << framed;
  ASSERT_TRUE(deframed->has_value()) << deframed << " framed=" << framed;
  EXPECT_EQ(enframe, **deframed) << " framed=" << framed;
  EXPECT_TRUE(framer->InputEmpty());
}

TEST_P(StreamFramerTest, DeframesOneByteAtATime) {
  ScopedSeverity severity(Severity::ERROR);
  auto framer = GetParam().make_framer();
  const auto enframe = GetParam().enframe;
  const auto framed = framer->Frame(enframe);
  for (auto c : framed) {
    auto early_pop = framer->Pop();
    EXPECT_TRUE(early_pop.is_ok()) << early_pop << " framed=" << framed;
    EXPECT_FALSE(early_pop->has_value()) << early_pop << " framed=" << framed;

    framer->Push(Slice::RepeatedChar(1, c));
  }
  auto deframed = framer->Pop();
  ASSERT_TRUE(deframed.is_ok()) << deframed << " framed=" << framed;
  ASSERT_TRUE(deframed->has_value()) << deframed << " framed=" << framed;
  EXPECT_EQ(enframe, **deframed) << " framed=" << framed;
  EXPECT_TRUE(framer->InputEmpty());
}

template <class T>
TestArg Test(Slice enframe) {
  return TestArg{[] { return std::make_unique<T>(); }, enframe};
}

INSTANTIATE_TEST_SUITE_P(
    StreamFramerSuite, StreamFramerTest,
    ::testing::Values(Test<ReliableFramer>(Slice::FromContainer({1, 2, 3})),
                      Test<UnreliableFramer>(Slice::FromContainer({1, 2, 3})),
                      Test<ReliableFramer>(Slice::RepeatedChar(256, 'a')),
                      Test<UnreliableFramer>(Slice::RepeatedChar(256, 'a')),
                      Test<ReliableFramer>(Slice::RepeatedChar(65536, 'a'))));

struct MultiArg {
  std::function<std::unique_ptr<StreamFramer>()> make_framer;
  std::vector<Slice> enframe;
};

struct StreamFramerMulti : public ::testing::TestWithParam<MultiArg> {};

template <class T>
MultiArg Multi(std::initializer_list<Slice> enframe) {
  return MultiArg{[] { return std::make_unique<T>(); }, enframe};
}

TEST_P(StreamFramerMulti, DeframesFramedOneFrameAtATime) {
  ScopedSeverity severity(Severity::ERROR);
  auto framer = GetParam().make_framer();
  for (auto enframe : GetParam().enframe) {
    const auto framed = framer->Frame(enframe);
    framer->Push(framed);
    auto deframed = framer->Pop();
    ASSERT_TRUE(deframed.is_ok()) << deframed << " framed=" << framed;
    ASSERT_TRUE(deframed->has_value()) << deframed << " framed=" << framed;
    EXPECT_EQ(enframe, **deframed) << " framed=" << framed;
    EXPECT_TRUE(framer->InputEmpty());
  }
}

TEST_P(StreamFramerMulti, DeframesOneByteAtATimeOneFrameAtATime) {
  ScopedSeverity severity(Severity::ERROR);
  auto framer = GetParam().make_framer();
  for (auto enframe : GetParam().enframe) {
    const auto framed = framer->Frame(enframe);
    for (auto c : framed) {
      auto early_pop = framer->Pop();
      EXPECT_TRUE(early_pop.is_ok()) << early_pop << " framed=" << framed;
      EXPECT_FALSE(early_pop->has_value()) << early_pop << " framed=" << framed;

      framer->Push(Slice::RepeatedChar(1, c));
    }
    auto deframed = framer->Pop();
    ASSERT_TRUE(deframed.is_ok()) << deframed << " framed=" << framed;
    ASSERT_TRUE(deframed->has_value()) << deframed << " framed=" << framed;
    EXPECT_EQ(enframe, **deframed) << " framed=" << framed;
    EXPECT_TRUE(framer->InputEmpty());
  }
}

TEST_P(StreamFramerMulti, DeframesFramedAllFramesAtOnce) {
  ScopedSeverity severity(Severity::ERROR);
  auto framer = GetParam().make_framer();
  for (auto enframe : GetParam().enframe) {
    const auto framed = framer->Frame(enframe);
    framer->Push(framed);
  }
  for (auto enframe : GetParam().enframe) {
    auto deframed = framer->Pop();
    ASSERT_TRUE(deframed.is_ok()) << deframed;
    ASSERT_TRUE(deframed->has_value()) << deframed;
    EXPECT_EQ(enframe, **deframed);
  }
  EXPECT_TRUE(framer->InputEmpty());
}

TEST_P(StreamFramerMulti, DeframesOneByteAtATimeAllFramesAtOnce) {
  ScopedSeverity severity(Severity::ERROR);
  auto framer = GetParam().make_framer();
  for (auto enframe : GetParam().enframe) {
    const auto framed = framer->Frame(enframe);
    for (auto c : framed) {
      framer->Push(Slice::RepeatedChar(1, c));
    }
  }
  for (auto enframe : GetParam().enframe) {
    auto deframed = framer->Pop();
    ASSERT_TRUE(deframed.is_ok()) << deframed;
    ASSERT_TRUE(deframed->has_value()) << deframed;
    EXPECT_EQ(enframe, **deframed);
  }
  EXPECT_TRUE(framer->InputEmpty());
}

INSTANTIATE_TEST_SUITE_P(
    StreamFramerMultiSuite, StreamFramerMulti,
    ::testing::Values(Multi<ReliableFramer>({Slice::FromContainer({1, 2, 3}),
                                             Slice::FromContainer({1, 2, 3})}),
                      Multi<UnreliableFramer>({Slice::FromContainer({1, 2, 3}),
                                               Slice::FromContainer({1, 2,
                                                                     3})})));

}  // namespace stream_framer_test
}  // namespace overnet
