// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TODO(https://fxbug.dev/84961): Fix null safety and remove this language version.
// @dart=2.9

import 'package:fxtest/fxtest.dart';
import 'package:fxutils/fxutils.dart';
import 'package:meta/meta.dart';

/// Helper which pairs positional arguments with any trailing `-a` arguments,
/// or, similarly, `-p` arguments with any trailing `-c` arguments.
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
  ///
  /// If the user executed:
  ///
  ///   > `fx test -p pkg-name -c comp-name`
  ///
  /// this value would unsurprisingly be:
  ///
  ///   > `[-p, pkg-name, -c, comp-name]`
  final List<String> rawArgs;

  /// Extracted testNames from the set of arguments passed to `fx test`. For
  /// example, if the user executed:
  ///
  ///   > `fx test test_one -a filter_one test_two --flag_one --flag_two`
  ///
  /// this value would be:
  ///
  ///   > `[test_one, test_two]`
  ///
  /// If the user executed:
  ///
  ///   > `fx test test_one -p pkg-name -c comp-name`
  ///
  /// this value would be:
  ///
  ///   > `[test_one]`
  final List<String> testNames;

  TestNamesCollector({
    /// The entire string of arguments passed to `fx test`.
    @required List<String> rawArgs,

    /// The extracted test names from all arguments passed to `fx test`.
    @required List<String> rawTestNames,
    @required String relativeCwd,
  })  : testNames =
            TestNamesCollector._processTestList(rawTestNames, relativeCwd),
        rawArgs = TestNamesCollector._processRawArgs(rawArgs, relativeCwd);

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
  ///   [<MatchableArgument "test_one">, <MatchableArgument "filter_one">],
  ///   [<MatchableArgument "test_two">, <MatchableArgument "filter_two">, <MatchableArgument "filter_two_b">],
  ///   [<MatchableArgument "test_three">]
  /// ]
  /// ```
  List<List<MatchableArgument>> collect() {
    List<List<MatchableArgument>> groupedTestFilters = [];
    var seenRootNames = <String>{};
    var args = _ListEmitter<String>(rawArgs);

    while (args.hasMore()) {
      var arg = args.take();
      if (seenRootNames.contains(arg)) {
        continue;
      }
      _ArgumentsProgress argsProgress;
      if (testNames.contains(arg)) {
        seenRootNames.add(arg);
        groupedTestFilters.add([MatchableArgument.unrestricted(arg)]);
        argsProgress = _takeAndFilters(
          args,
          testNames,
        );
      } else if (['--package', '-p'].contains(arg)) {
        // Break out if we're at the end and had a dangling `-p` flag.
        if (!args.hasMore()) break;

        var nextArg = args.take();
        groupedTestFilters.add([MatchableArgument.packageName(nextArg)]);
        argsProgress = _takeComponentFilters(
          args,
          testNames,
        );
      } else if (['--component', '-c'].contains(arg)) {
        // Break out if we're at the end and had a dangling `-c` flag.
        if (!args.hasMore()) break;

        var nextArg = args.take();
        groupedTestFilters.add([MatchableArgument.componentName(nextArg)]);
      }
      if (argsProgress != null) {
        if (argsProgress.additionalFilters.isNotEmpty) {
          groupedTestFilters[groupedTestFilters.length - 1]
              .addAll(argsProgress.additionalFilters);
        }
        args = argsProgress.args;
      }
    }
    if (groupedTestFilters.isEmpty) {
      groupedTestFilters.add([MatchableArgument.empty()]);
    }
    return groupedTestFilters;
  }

  _ArgumentsProgress _takeComponentFilters(
    _ListEmitter<String> args,
    List<String> testNames,
  ) {
    final additionalFilters = <MatchableArgument>[];

    // Stop 1 item early because a trailing `-c` flag is definitionally
    // not followed by another token
    while (args.hasNMore(2)) {
      var arg = args.peek();
      if (['--component', '-c'].contains(arg)) {
        // Confirm our peek
        args.take();
        var nextArg = args.take();
        additionalFilters.add(MatchableArgument.componentName(nextArg));
      } else {
        break;
      }
    }
    return _ArgumentsProgress(args, additionalFilters);
  }

  _ArgumentsProgress _takeAndFilters(
    _ListEmitter<String> args,
    List<String> testNames,
  ) {
    final additionalFilters = <MatchableArgument>[];

    // Stop 1 item early because a trailing `-a` flag is definitionally
    // not followed by another token
    while (args.hasNMore(2)) {
      var arg = args.peek();
      if (['--all', '-a'].contains(arg)) {
        // Confirm our peek
        args.take();
        var nextFilter = args.take();
        additionalFilters.addAll(
          nextFilter
              .split(',')
              .map((testName) => MatchableArgument.unrestricted(testName))
              .toList(),
        );
      } else {
        break;
      }
    }
    return _ArgumentsProgress(args, additionalFilters);
  }

  static List<String> _processTestList(
    List<String> testList,
    String relativeCwd,
  ) {
    // Replace a test name that is merely "." with the actual current directory
    // Use a set-to-list maneuver to remove duplicates
    return {
      for (var testName in testList)
        testName == cwdToken ? relativeCwd : testName
    }.toList();
  }

  static List<String> _processRawArgs(
    List<String> rawArgs,
    String relativeCwd,
  ) {
    // Replace a test name that is merely "." with the actual current directory
    // Use a set-to-list maneuver to remove duplicates
    return [for (var arg in rawArgs) arg == cwdToken ? relativeCwd : arg];
  }
}

/// Helper that tracks progress through the positionally-aware parsing of
/// raw arguments.
class _ArgumentsProgress {
  final _ListEmitter<String> args;
  final List<MatchableArgument> additionalFilters;
  _ArgumentsProgress(this.args, this.additionalFilters);
}

class _ListEmitter<T> {
  final List<T> _list;
  int _counter = 0;
  _ListEmitter(this._list);

  /// Look at the next item in the list without advancing the marker.
  T peek() => _list.length > _counter ? _list[_counter] : null;

  /// Look at the next item in the list and advance the marker.
  T take() {
    if (_counter == _list.length) return null;
    _counter += 1;
    return _list[_counter - 1];
  }

  bool hasMore() => hasNMore(1);
  bool hasNMore(int numberMore) => _list.length - _counter >= numberMore;
}

enum MatchType {
  /// Default rule which can match against any portion of a [TestDefinition].
  unrestricted,

  /// Specialized rule which indicates only matching against package names.
  packageName,

  /// Specialized rule which indicates only matching against component names.
  componentName,
}

/// Container for a [String] used to match against different parts of test names
/// with an additional [MatchType] attribute which optionally specifies
/// limitations on parts of a [TestDefinition] eligible for matching.
class MatchableArgument {
  final String arg;
  final MatchType matchType;

  /// Generic constructor which accepts all paramters. Not preferred for
  /// external use.
  MatchableArgument._(this.arg, this.matchType);

  /// Helper constructor for unrestricted matchers.
  factory MatchableArgument.unrestricted(String arg) =>
      MatchableArgument._(arg, MatchType.unrestricted);

  /// Helper constructor for package-name matchers.
  factory MatchableArgument.packageName(String arg) =>
      MatchableArgument._(arg, MatchType.packageName);

  /// Helper constructor for component-name matchers.
  factory MatchableArgument.componentName(String arg) =>
      MatchableArgument._(arg, MatchType.componentName);

  /// Helper constructor for when there are zero test name tokens whatsoever.
  factory MatchableArgument.empty() =>
      MatchableArgument._(null, MatchType.unrestricted);

  @override
  bool operator ==(dynamic other) {
    return identical(this, other) ||
        (other is MatchableArgument &&
            runtimeType == other.runtimeType &&
            arg == other.arg &&
            matchType == other.matchType);
  }

  @override
  int get hashCode => arg.hashCode ^ matchType.hashCode;

  @override
  String toString() =>
      '<MatchableArgument ${matchType.toString().split('.')[1]}::"$arg" />';
}
