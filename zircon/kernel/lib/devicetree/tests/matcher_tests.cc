// Copyright 2022 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/devicetree/devicetree.h>
#include <lib/devicetree/internal/matcher.h>
#include <lib/devicetree/matcher.h>
#include <lib/devicetree/path.h>
#include <lib/stdcompat/array.h>

#include <cstdint>

#include <zxtest/zxtest.h>

#include "test_helper.h"

namespace {

template <size_t N>
struct AbsoluteMatcher {
  devicetree::MatcherScanResult<N> operator()(const devicetree::NodePath&, devicetree::Properties) {
    return {devicetree::MatcherResult::kDone};
  }
};
static_assert(devicetree::internal::kRequestedScans<AbsoluteMatcher<1>> == 1);
static_assert(devicetree::internal::kRequestedScans<AbsoluteMatcher<2>> == 2);
static_assert(devicetree::internal::kRequestedScans<AbsoluteMatcher<3>> == 3);
static_assert(devicetree::internal::kRequestedScans<AbsoluteMatcher<4>> == 4);

template <size_t N>
struct RelativeMatcher {
  devicetree::MatcherScanResult<N> operator()(const devicetree::NodePath&, devicetree::Properties,
                                              const devicetree::PathResolver&) {
    return {devicetree::MatcherResult::kDone};
  }
};
static_assert(devicetree::internal::kRequestedScans<RelativeMatcher<1>> == 2);
static_assert(devicetree::internal::kRequestedScans<RelativeMatcher<2>> == 3);
static_assert(devicetree::internal::kRequestedScans<RelativeMatcher<3>> == 4);
static_assert(devicetree::internal::kRequestedScans<RelativeMatcher<4>> == 5);

static_assert(devicetree::internal::kMaxRequestedScans<RelativeMatcher<1>, AbsoluteMatcher<2>,
                                                       RelativeMatcher<1>, AbsoluteMatcher<1111>> ==
              1111);

constexpr size_t kMaxSize = 1024;

class MatchTest : public zxtest::Test {
 public:
  static void SetUpTestSuite() {
    ASSERT_NO_FATAL_FAILURE(ReadTestData("complex_no_properties.dtb", fdt_no_props));
    ASSERT_NO_FATAL_FAILURE(ReadTestData("complex_with_alias.dtb", fdt_with_alias));
    ASSERT_NO_FATAL_FAILURE(ReadTestData("complex_with_alias_first.dtb", fdt_with_alias_first));
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

  /*
         *
      / \   \
     A   E   aliases
    / \   \
   B   C   F
      /   / \
     D   G   I
        /
       H

  aliases:
    foo = /A/C
    bar = /E/F
*/
  devicetree::Devicetree with_alias_tree() {
    return devicetree::Devicetree({fdt_with_alias.data(), fdt_with_alias.size()});
  }

  /*
          *
     /     / \
   aliases A   E
          / \   \
         B   C   F
            /   / \
           D   G   I
              /
             H

aliases:
  foo = /A/C
  bar = /E/F
*/
  devicetree::Devicetree with_alias_first_tree() {
    return devicetree::Devicetree({fdt_with_alias_first.data(), fdt_with_alias_first.size()});
  }

 private:
  static std::array<uint8_t, kMaxSize> fdt_no_props;
  static std::array<uint8_t, kMaxSize> fdt_with_alias;
  static std::array<uint8_t, kMaxSize> fdt_with_alias_first;
};

std::array<uint8_t, kMaxSize> MatchTest::fdt_no_props = {};
std::array<uint8_t, kMaxSize> MatchTest::fdt_with_alias = {};
std::array<uint8_t, kMaxSize> MatchTest::fdt_with_alias_first = {};

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
  auto match_result = devicetree::Match(tree, matcher);
  ASSERT_TRUE(match_result.is_ok());
  ASSERT_EQ(*match_result, 1);

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

  auto match_result = devicetree::Match(tree, matcher, matcher_2);
  ASSERT_TRUE(match_result.is_ok());
  ASSERT_EQ(*match_result, 1);

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
  ASSERT_TRUE(scan_result.is_ok());
  ASSERT_EQ(*scan_result, 2);

  EXPECT_EQ(called, 1);
  EXPECT_EQ(called_2, 2);
}

TEST_F(MatchTest, SingleMatcherNeveDoneCompletes) {
  {
    SingleNodeMatcher<1> matcher{.path_to_match = "/A/C/D/G", .cb = [](auto name, auto props) {
                                   FAIL("This matcher should not match anything.");
                                 }};

    auto tree = no_prop_tree();

    // If not all matchers are done, returns error with index of the failing matcher (0).
    auto match_res = devicetree::Match(tree, matcher);
    ASSERT_TRUE(match_res.is_error());
    ASSERT_EQ(match_res.error_value(), 0);
    EXPECT_FALSE(matcher.found);
    // The matcher gets called for every node in the path, plus every offspring that
    // branches out of the path, per scan. The default number of scan is 1.
    EXPECT_EQ(matcher.visit_count, 6);
  }
}

template <size_t Rescans>
struct SingleNodeMatcherWithAlias {
  devicetree::MatcherScanResult<Rescans> operator()(const devicetree::NodePath& path,
                                                    devicetree::Properties props,
                                                    const devicetree::PathResolver& resolver) {
    visit_count++;
    auto resolved_path = resolver.Resolve(path_to_match);
    if (resolved_path.is_error()) {
      if (resolved_path.error_value() == devicetree::PathResolver::ResolveError::kBadAlias) {
        return {devicetree::MatcherResult::kAvoidSubtree};
      }
      return {devicetree::MatcherResult::kNeedsAliases};
    }
    switch (devicetree::ComparePath(path, *resolved_path)) {
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

TEST_F(MatchTest, MatcherWithAliasBailsEarlyWithoutAlias) {
  SingleNodeMatcherWithAlias<1> matcher{
      .path_to_match = "A/C/D/G",
      .cb = [](auto name, auto props) { FAIL("This matcher should not match anything."); }};

  auto tree = no_prop_tree();

  // If not all matchers are done, returns index of failing matcher.
  auto match_res = devicetree::Match(tree, matcher);
  ASSERT_TRUE(match_res.is_error());
  ASSERT_EQ(match_res.error_value(), 0);

  // It will never be found, and it will be pruned at the root of each tree.
  EXPECT_FALSE(matcher.found);
  EXPECT_EQ(matcher.visit_count, 1);
}

TEST_F(MatchTest, MultipleMatchersWithAlias) {
  SingleNodeMatcherWithAlias<1> matcher{.path_to_match = "foo/D",
                                        .cb = [](auto name, auto props) { EXPECT_EQ(name, "D"); }};
  SingleNodeMatcherWithAlias<1> matcher_2{
      .path_to_match = "bar/G/H", .cb = [](auto name, auto props) { EXPECT_EQ(name, "H"); }};

  auto tree = with_alias_tree();

  auto match_res = devicetree::Match(tree, matcher, matcher_2);
  ASSERT_TRUE(match_res.is_ok());
  ASSERT_EQ(*match_res, 2);

  EXPECT_TRUE(matcher.found);
  EXPECT_EQ(matcher.visit_count, 6);

  EXPECT_TRUE(matcher_2.found);
  EXPECT_EQ(matcher_2.visit_count, 7);
}

TEST_F(MatchTest, MultipleMatchersAliasResolvedFirstIsSingleScan) {
  SingleNodeMatcherWithAlias<1> matcher{.path_to_match = "foo/D",
                                        .cb = [](auto name, auto props) { EXPECT_EQ(name, "D"); }};
  SingleNodeMatcherWithAlias<1> matcher_2{
      .path_to_match = "bar/G/H", .cb = [](auto name, auto props) { EXPECT_EQ(name, "H"); }};

  auto tree = with_alias_first_tree();

  auto match_res = devicetree::Match(tree, matcher, matcher_2);
  ASSERT_TRUE(match_res.is_ok());
  ASSERT_EQ(*match_res, 1);

  EXPECT_TRUE(matcher.found);
  EXPECT_EQ(matcher.visit_count, 5);

  EXPECT_TRUE(matcher_2.found);
  EXPECT_EQ(matcher_2.visit_count, 6);
}

TEST_F(MatchTest, LambdaAsMatchersMixedAliasAndNoAlias) {
  int called = 0;
  int called_2 = 0;

  auto tree = with_alias_tree();

  auto scan_result = devicetree::Match(
      tree,
      [&called](const devicetree::NodePath& path, devicetree::Properties props,
                const devicetree::PathResolver& resolver) -> devicetree::MatcherScanResult<2> {
        auto resolved_path = resolver.Resolve("foo/D");
        if (resolved_path.is_error()) {
          return {resolved_path.error_value() == devicetree::PathResolver::ResolveError::kBadAlias
                      ? devicetree::MatcherResult::kDone
                      : devicetree::MatcherResult::kNeedsAliases};
        }
        switch (devicetree::ComparePath(path, *resolved_path)) {
          case devicetree::CompareResult::kIsMatch:
            ++called;
            return {devicetree::MatcherResult::kDone};
          case devicetree::CompareResult::kIsAncestor:
            return {devicetree::MatcherResult::kVisitSubtree};
          default:
            return {devicetree::MatcherResult::kAvoidSubtree};
        }
      },
      [&called_2](const devicetree::NodePath& path, devicetree::Properties props,
                  const devicetree::PathResolver& resolver) -> devicetree::MatcherScanResult<2> {
        switch (devicetree::ComparePath(path, "/A/C/D")) {
          case devicetree::CompareResult::kIsMatch:
            ++called_2;
            return {(called_2 == 2) ? devicetree::MatcherResult::kDone
                                    : devicetree::MatcherResult::kAvoidSubtree};
          case devicetree::CompareResult::kIsAncestor:
            return {devicetree::MatcherResult::kVisitSubtree};
          default:
            return {devicetree::MatcherResult::kAvoidSubtree};
        }
      });

  ASSERT_TRUE(scan_result.is_ok());
  ASSERT_EQ(*scan_result, 2);

  EXPECT_EQ(called, 1);
  EXPECT_EQ(called_2, 2);
}

template <size_t MaxScans, size_t ScansForDone>
struct UnboundedMatcher {
  devicetree::MatcherScanResult<MaxScans> operator()(const devicetree::NodePath&,
                                                     devicetree::Properties) {
    visit_count++;
    return {devicetree::MatcherResult::kVisitSubtree};
  }

  devicetree::MatcherResult operator()() {
    scan_count++;
    return scan_count >= ScansForDone ? devicetree::MatcherResult::kDone
                                      : devicetree::MatcherResult::kVisitSubtree;
  }

  size_t visit_count = 0;
  size_t scan_count = 0;
};

TEST_F(MatchTest, MatcherNotifiedOnScanEndMatcherDidNotFinish) {
  constexpr size_t kScans = 3;

  auto tree = no_prop_tree();
  UnboundedMatcher<kScans, kScans + 1> matcher;

  auto match_res = devicetree::Match(tree, matcher);
  ASSERT_TRUE(match_res.is_error());
  ASSERT_EQ(match_res.error_value(), 0);

  EXPECT_EQ(matcher.visit_count, 10 * kScans);
  EXPECT_EQ(matcher.scan_count, kScans);
}

TEST_F(MatchTest, MatcherNotifiedOnScanEndMatcherFinished) {
  constexpr size_t kScans = 3;

  auto tree = no_prop_tree();
  UnboundedMatcher<kScans, kScans> matcher;

  auto match_res = devicetree::Match(tree, matcher);
  ASSERT_TRUE(match_res.is_ok());
  ASSERT_EQ(*match_res, kScans);

  EXPECT_EQ(matcher.visit_count, 10 * kScans);
  EXPECT_EQ(matcher.scan_count, kScans);
}

TEST_F(MatchTest, MatcherDoesntFinishWithinRequestedScanIsAbortsEarly) {
  auto tree = no_prop_tree();

  UnboundedMatcher<2, 2> done_after_second_scan;

  auto match_result = devicetree::Match(
      tree,
      // We will bail early, and this matcher wont complete either, since only
      // a single scan will be completed.
      done_after_second_scan,
      // This matcher will neve complete, but claims it should complete after 1 scan.
      [](const auto&, auto) { return devicetree::MatcherResult::kVisitSubtree; });

  ASSERT_TRUE(match_result.is_error());
  EXPECT_EQ(match_result.error_value(), 1);
  EXPECT_EQ(done_after_second_scan.visit_count, 10);
  EXPECT_EQ(done_after_second_scan.scan_count, 1);
}

}  // namespace
