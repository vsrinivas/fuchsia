// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:fxtest/fxtest.dart';

/// Distills a list of lists with potential duplicates down to a single list
/// of unique values.
///
/// Also translates any test name value of "." to the current working directory,
/// since we can safely assume "." will never be an actual test name.
class TestNamesCollector {
  final List<List<String>> testNamesLists;
  final FuchsiaLocator fuchsiaLocator;
  TestNamesCollector(this.testNamesLists, {fuchsiaLocator})
      : fuchsiaLocator = fuchsiaLocator ?? FuchsiaLocator.shared;

  /// Distill the list of lists into a single list with no duplicates.
  List<String> collect() {
    // Combine all requested test patterns, whether specified as extra arguments
    // or with the `-t` flag
    return {
      for (var testNamesList in testNamesLists) //
        ..._processTestList(testNamesList)
    }.cast<String>().toList();
  }

  List<String> _processTestList(List<String> testList) {
    // Replace a test name that is merely "." with the actual current directory
    return [
      for (var testName in testList)
        testName == cwdToken ? fuchsiaLocator.relativeCwd : testName
    ];
  }
}
