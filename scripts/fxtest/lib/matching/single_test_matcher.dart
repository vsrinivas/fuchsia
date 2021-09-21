// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TODO(https://fxbug.dev/84961): Fix null safety and remove this language version.
// @dart=2.9

import 'package:fxtest/fxtest.dart';
import 'package:meta/meta.dart';

class SingleTestMatcher {
  List<TestAttributeMatcher> matchers;
  SingleTestMatcher()
      : matchers = <TestAttributeMatcher>[
          NoArgumentsMatcher(),
          PathMatcher(),
          PackageNameMatcher(),
          ComponentNameMatcher(),
          PackageUrlMatcher(),
          NameMatcher(),
          LabelMatcher(),
          RuntimeDepsMatcher(),
        ];

  ComparisonResult evaluateTestAgainstArguments(
    TestDefinition testDefinition,
    PermutatedTestsConfig testsConfig, {
    @required MatchLength matchLength,
    @required Comparer comparer,
  }) {
    var results = <ComparisonResult>[];
    var testNameGroup = testsConfig.testNameGroup;
    // Start by looping over all arguments in this group of arguments (a group
    // of arguments are test names joined by `--and` or `-p|-c`)
    for (MatchableArgument matchable in testNameGroup) {
      // To begin, each matchable is declared as unmatched
      bool hasMatchedTestName = false;
      // Give each checker a chance to match against the rule
      for (var matcher in matchers) {
        // When a matcher matches a testName, mark it and break out of the inner
        // loop, since additional matches for that testName are meaningless
        var comparisonResult = matcher.isAttributeMatch(
          matchable?.arg?.toLowerCase(),
          testDefinition,
          comparer: comparer,
          matchLength: matchLength,
          matchType: matchable?.matchType,
          flags: testsConfig.flags,
        );
        if (comparisonResult.isMatch) {
          hasMatchedTestName = true;
          results.add(comparisonResult);
          break;
        }
      }
      // We found no checkers for this rule that matched against this filter,
      // which means the provided filter is meant to exclude this test
      if (!hasMatchedTestName) {
        return ComparisonResult.failure;
      }
    }
    return ComparisonResult.fromAverage(results);
  }
}
