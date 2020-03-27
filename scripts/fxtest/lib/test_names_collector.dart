// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:fxtest/fxtest.dart';
import 'package:meta/meta.dart';

/// Helper which pairs positional test name arguments with any trailing `-a`
/// arguments.
///
/// Also translates any test name value of "." to the current working directory,
/// since we can safely assume "." will never be an actual test name.
///
/// There is some redudancy in the values passed to [rawArgs] and [testNames]
/// due to details of the `ArgParser`'s API. This stems from the fact that args
/// are all parsed into their respective buckets, but original ordering is lost.
/// Thus, we know all the floating arguments passed (testNames), and which values
/// were passed under a `-a` flag, but not which [testNames] those additional
/// `-a` filters immediately followed. Because of this, we take both the
/// extracted [testNames] and the original raw, unparsed series of CLI arguments,
/// and reverse-engineer what we need.
class TestNamesCollector {
  /// All arguments passed to `fx test`. For example, if the user executed:
  ///
  ///   > `fx test test_one -a filter_one test_two --flag_one --flag_two`
  ///
  /// this value would be:
  ///
  ///   > `[test_one, -a, filter_one, test_two, --flag_one, --flag_two,]`
  final List<String> rawArgs;

  /// Extracted testNames from the set of arguments passed to `fx test`. For
  /// example, if the user executed:
  ///
  ///   > `fx test test_one -a filter_one test_two --flag_one --flag_two`
  ///
  /// this value would be:
  ///
  ///   > `[test_one, test_two]`
  final List<String> testNames;

  TestNamesCollector({
    /// The entire string of arguments passed to `fx test`.
    @required List<String> rawArgs,

    /// The extracted test names from all arguments passed to `fx test`.
    @required List<String> rawTestNames,
    fuchsiaLocator,
  })  : testNames = TestNamesCollector._processTestList(
          rawTestNames,
          fuchsiaLocator ?? FuchsiaLocator.shared,
        ),
        rawArgs = TestNamesCollector._processRawArgs(
          rawArgs,
          fuchsiaLocator ?? FuchsiaLocator.shared,
        );

  /// Loops over a combination of the original, raw list of arguments, and the
  /// parsed arguments, to determine which positional arguments are modified by
  /// a trailing `--a|-a` flag. For example,
  ///
  /// ```
  /// test_one -a filter_one test_two -a filter_two -a filter_two_b test_three
  /// ```
  ///
  /// Should be parsed into:
  ///
  /// ```
  /// [
  ///   [test_one, filter_one],
  ///   [test_two, filter_two, filter_two_b],
  ///   [test_three]
  /// ]
  /// ```
  List<List<String>> collect() {
    List<List<String>> groupedTestFilters = [];
    var seenRootNames = <String>{};

    if (testNames.isEmpty) {
      return groupedTestFilters;
    }

    int counter = 0;
    while (counter < rawArgs.length) {
      var arg = rawArgs[counter];
      counter += 1;
      if (seenRootNames.contains(arg)) {
        continue;
      }
      if (testNames.contains(arg)) {
        seenRootNames.add(arg);
        groupedTestFilters.add([arg]);
      }
      _ArgumentsProgress argsProgress = _takeAdditionalFilters(
        counter,
        rawArgs,
        testNames,
      );
      counter = argsProgress.counter;
      if (argsProgress.additionalFilters.isNotEmpty) {
        groupedTestFilters[groupedTestFilters.length - 1]
            .addAll(argsProgress.additionalFilters);
      }
    }
    return groupedTestFilters;
  }

  _ArgumentsProgress _takeAdditionalFilters(
    int counter,
    List<String> args,
    List<String> testNames,
  ) {
    int _counter = counter;
    final additionalFilters = <String>[];

    // ignore: literal_only_boolean_expressions
    while (true) {
      // Stop 1 item early because a trailing `-a` flag is definitionally not
      // followed by another token
      if (_counter >= args.length - 1) {
        break;
      }
      var arg = args[_counter];
      if (['--all', '-a'].contains(arg)) {
        var nextFilter = args[_counter + 1];
        additionalFilters.addAll(nextFilter.split(','));
        _counter += 1;
      } else if (testNames.contains(arg)) {
        break;
      }
      _counter += 1;
    }
    return _ArgumentsProgress(_counter, additionalFilters);
  }

  static List<String> _processTestList(
    List<String> testList,
    FuchsiaLocator fuchsiaLocator,
  ) {
    // Replace a test name that is merely "." with the actual current directory
    // Use a set-to-list maneuver to remove duplicates
    return {
      for (var testName in testList)
        testName == cwdToken ? fuchsiaLocator.relativeCwd : testName
    }.toList();
  }

  static List<String> _processRawArgs(
    List<String> rawArgs,
    FuchsiaLocator fuchsiaLocator,
  ) {
    // Replace a test name that is merely "." with the actual current directory
    // Use a set-to-list maneuver to remove duplicates
    return [
      for (var arg in rawArgs)
        arg == cwdToken ? fuchsiaLocator.relativeCwd : arg
    ];
  }
}

class _ArgumentsProgress {
  final int counter;
  final List<String> additionalFilters;
  _ArgumentsProgress(this.counter, this.additionalFilters);
}
