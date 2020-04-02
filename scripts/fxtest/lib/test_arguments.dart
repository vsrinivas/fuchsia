// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:args/args.dart';
import 'package:fxtest/fxtest.dart';

class TestArguments {
  final List<String> rawArgs;

  /// Arguments hydrated into their respective data type representations
  /// ([bool], [String], [int], etc).
  final ArgResults parsedArgs;

  /// Raw string arguments to be forwarded down to each executed test.
  ///
  /// These tokens have no impact whatsoever on how `fx test` finds, filters,
  /// and invokes tests. They are exclusively used by the underlying tests.
  final List<String> passThroughArgs;

  TestArguments({this.rawArgs})
      : passThroughArgs = TestArguments._extractPassThroughArgs(
          rawArgs,
        ),
        parsedArgs = TestArguments._parseArgs(rawArgs);

  /// Splits a list of command line arguments into the half intended for
  /// local use and the half intended to be passed through to sub-commands.
  static List<List<String>> splitArgs(List<String> rawArgs) {
    var dashDashIndex = rawArgs.indexOf('--');
    if (dashDashIndex == -1) {
      dashDashIndex = rawArgs.length;
    }
    return [
      rawArgs.take(dashDashIndex).toList(),
      rawArgs.skip(dashDashIndex + 1).toList(),
    ];
  }

  static List<String> _extractPassThroughArgs(List<String> rawArgs) {
    return TestArguments.splitArgs(rawArgs)[1];
  }

  static ArgResults _parseArgs(
    List<String> rawArgs,
  ) {
    var localArgs = TestArguments.splitArgs(rawArgs)[0];
    return fxTestArgParser.parse(localArgs);
  }
}
