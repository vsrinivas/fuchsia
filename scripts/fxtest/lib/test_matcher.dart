// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:fxtest/fxtest.dart';

class TestMatcher {
  List<Checker> checkers;
  TestMatcher({this.checkers});

  bool matches(
    TestDefinition testDefinition,
    PermutatedTestsConfig testConfig, {
    bool exactMatching,
  }) {
    // Start by looping over all provided rules in this group
    for (String testName in testConfig.testNameGroup ?? [null]) {
      // To begin, each testName is declared as unmatched
      bool hasMatchedTestName = false;
      // Give each checker a chance to match against the rule
      for (var checker in checkers) {
        // When a checker matches a testName, mark it and break out of the inner
        // loop, since additional matches for that testName are meaningless
        if (checker.canHandle(testName, testConfig.flags, testDefinition,
            exactMatching: exactMatching)) {
          hasMatchedTestName = true;
          break;
        }
      }
      // We found no checkers for this rule that matched against this filter,
      // which means the provided filter is meant to exclude this test
      if (!hasMatchedTestName) {
        return false;
      }
    }
    // All filters/test names were matched, which means this test should be
    // run against the provided rules
    return true;
  }
}
