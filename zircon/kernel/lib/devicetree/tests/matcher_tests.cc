// Copyright 2022 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/devicetree/matcher.h>
#include <lib/devicetree/path.h>
#include <lib/stdcompat/array.h>

#include <zxtest/zxtest.h>

#include "lib/devicetree/internal/matcher.h"
#include "lib/devicetree/matcher-result.h"
#include "test_helper.h"

namespace {

constexpr size_t kMaxSize = 1024;

class MatchTest : public zxtest::Test {
 public:
  static void SetUpTestSuite() {
    ASSERT_NO_FATAL_FAILURE(ReadTestData("complex_no_properties.dtb", fdt_no_props));
  }

  /*
         *
        / \
       A   E
      / \   \
     B   C   F
        /   / \
       D   G   I
          /
         H
  */
  devicetree::Devicetree no_prop_tree() {
    return devicetree::Devicetree({fdt_no_props.data(), fdt_no_props.size()});
  }

 private:
  static std::array<uint8_t, kMaxSize> fdt_no_props;
};

std::array<uint8_t, kMaxSize> MatchTest::fdt_no_props = {};

template <size_t Rescans>
struct SingleNodeMatcher {
  devicetree::MatcherScanResult<Rescans> operator()(const devicetree::NodePath& path,
                                                    devicetree::Properties props) {
    visit_count++;

    switch (devicetree::ComparePath(path, path_to_match)) {
      case devicetree::kIsMatch:
        found = true;
        cb(path.back(), props);
        return {devicetree::MatcherResult::kDone};
      case devicetree::kIsAncestor:
        return {devicetree::MatcherResult::kVisitSubtree};
      case devicetree::kIsMismatch:
        return {devicetree::MatcherResult::kAvoidSubtree};
      case devicetree::kIsDescendant:
        return {devicetree::MatcherResult::kAvoidSubtree};
    };
  }

  std::string_view path_to_match;
  std::function<void(std::string_view node_name, devicetree::Properties)> cb =
      [](std::string_view node_name, devicetree::Properties props) {};
  bool found = false;
  bool fatal_error = false;
  int visit_count = 0;
};

TEST_F(MatchTest, SingleMatcherNoAlias) {
  size_t seen = 0;
  SingleNodeMatcher<2> matcher{.path_to_match = "/A/C/D", .cb = [&](auto name, auto props) {
                                 seen++;
                                 EXPECT_EQ(name, "D");
                               }};

  auto tree = no_prop_tree();

  ASSERT_EQ(devicetree::Match(tree, matcher), 1);

  EXPECT_TRUE(matcher.found);
  EXPECT_EQ(matcher.visit_count, 5);
  EXPECT_EQ(seen, 1);
}

TEST_F(MatchTest, MultipleMatchersNoAlias) {
  size_t d_seen = 0;
  size_t h_seen = 0;
  // Matchers are disjoint, so they both prune one section of the tree, hence we validate that
  // a walk over a subtree is performed as long as at least one matcher is interested in the
  // path, not pruned.
  SingleNodeMatcher<1> matcher{.path_to_match = "/A/C/D", .cb = [&](auto name, auto props) {
                                 d_seen++;
                                 EXPECT_EQ(name, "D");
                               }};
  SingleNodeMatcher<1> matcher_2{.path_to_match = "/E/F/G/H", .cb = [&](auto name, auto props) {
                                   h_seen++;
                                   EXPECT_EQ(name, "H");
                                 }};

  auto tree = no_prop_tree();

  ASSERT_EQ(devicetree::Match(tree, matcher, matcher_2), 1);

  EXPECT_TRUE(matcher.found);
  EXPECT_EQ(matcher.visit_count, 5);

  EXPECT_TRUE(matcher_2.found);
  EXPECT_EQ(matcher_2.visit_count, 6);

  EXPECT_EQ(d_seen, 1);
  EXPECT_EQ(h_seen, 1);
}

TEST_F(MatchTest, LambdaAsMatchers) {
  int called = 0;
  int called_2 = 0;
  auto tree = no_prop_tree();

  auto scan_result = devicetree::Match(
      tree,
      [&called](const devicetree::NodePath& path, devicetree::Properties props) {
        using Result = devicetree::MatcherResult;
        switch (devicetree::ComparePath(path, "/A/C/D")) {
          case devicetree::CompareResult::kIsMatch:
            ++called;
            return Result::kDone;
          case devicetree::CompareResult::kIsAncestor:
            return Result::kVisitSubtree;
          default:
            return Result::kAvoidSubtree;
        }
      },
      [&called_2](const devicetree::NodePath& path,
                  devicetree::Properties props) -> devicetree::MatcherScanResult<2> {
        using Result = devicetree::MatcherResult;
        switch (devicetree::ComparePath(path, "/E/F/I")) {
          case devicetree::CompareResult::kIsMatch:
            ++called_2;
            return {(called_2 == 2) ? Result::kDone : Result::kAvoidSubtree};
          case devicetree::CompareResult::kIsAncestor:
            return {Result::kVisitSubtree};
          default:
            return {Result::kAvoidSubtree};
        }
      });

  // If not all matchers are done, returns -1.
  ASSERT_EQ(scan_result, 2);

  EXPECT_EQ(called, 1);
  EXPECT_EQ(called_2, 2);
}

TEST_F(MatchTest, SingleMatcherNeveDoneCompletes) {
  SingleNodeMatcher<1> matcher{.path_to_match = "/A/C/D/G", .cb = [](auto name, auto props) {
                                 FAIL("This matcher should not match anything.");
                               }};

  auto tree = no_prop_tree();

  // If not all matchers are done, returns -1.
  ASSERT_EQ(devicetree::Match(tree, matcher), -1);
  EXPECT_FALSE(matcher.found);
  // The matcher gets called for every node in the path, plus every offspring that
  // branches out of the path, per scan. The default number of scan is 1.
  EXPECT_EQ(matcher.visit_count, 6);
}

}  // namespace
