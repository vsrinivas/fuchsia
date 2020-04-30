// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:args/args.dart';
import 'package:fxtest/fxtest.dart';
import 'package:io/ansi.dart' as ansi;
import 'package:meta/meta.dart';

// ignore: prefer_generic_function_type_aliases
typedef String Stylizer(String value, Iterable<ansi.AnsiCode> codes,
    {bool forScript});

/// Simple class to hold shared parameters.
class Flags {
  final bool dryRun;
  final bool isVerbose;

  /// The maximum number of tests to run. If 0, all tests will be executed.
  final int limit;

  /// The realm name to run the test inside of. If null, a random name is used.
  final String realm;
  final bool allOutput;
  final bool shouldRebuild;

  /// Extra tokens to be passed through to individual tests.
  final bool exactMatches;
  final bool infoOnly;
  final bool shouldFailFast;
  final bool simpleOutput;
  final bool shouldOnlyRunDeviceTests;
  final bool shouldOnlyRunHostTests;
  final bool shouldPrintSkipped;
  final bool shouldRandomizeTestOrder;
  final bool shouldSilenceUnsupported;
  final bool shouldUpdateIfInBase;
  final int slowThreshold;
  Flags({
    this.dryRun = false,
    this.isVerbose = false,
    this.limit = 0,
    this.realm,
    this.allOutput = false,
    this.exactMatches = false,
    this.infoOnly = false,
    this.simpleOutput = false,
    this.shouldFailFast = false,
    this.shouldOnlyRunDeviceTests = false,
    this.shouldOnlyRunHostTests = false,
    this.shouldPrintSkipped = false,
    this.shouldRandomizeTestOrder = false,
    this.shouldRebuild = true,
    this.shouldSilenceUnsupported = false,
    this.shouldUpdateIfInBase = true,
    this.slowThreshold = 0,
  });

  factory Flags.fromArgResults(ArgResults argResults) {
    return Flags(
      allOutput: argResults['output'],
      dryRun: argResults['info'] || argResults['dry'],
      exactMatches: argResults['exact'],
      infoOnly: argResults['info'],
      isVerbose: argResults['verbose'] || argResults['output'],
      limit: int.parse(argResults['limit'] ?? '0'),
      realm: argResults['realm'],
      simpleOutput: argResults['simple'],
      shouldFailFast: argResults['fail'],
      shouldOnlyRunDeviceTests: argResults['device'],
      shouldOnlyRunHostTests: argResults['host'],
      shouldPrintSkipped: argResults['skipped'],

      // True (aka, yes rebuild) if `no-build` is missing or set to `False`
      shouldRebuild: (!argResults['info'] && !argResults['dry']) &&
          (argResults['build'] == null || argResults['build']),
      shouldRandomizeTestOrder: argResults['random'],
      shouldSilenceUnsupported: argResults['silenceunsupported'],
      shouldUpdateIfInBase: argResults['updateifinbase'],
      slowThreshold: int.parse(argResults['slow'] ?? '0'),
    );
  }

  @override
  String toString() => '''<Flags
  dryRun: $dryRun
  allOutput: $allOutput,
  limit: $limit
  realm: $realm
  isVerbose: $isVerbose
  exactMatches: $exactMatches,
  info: $infoOnly,
  simpleOutput: $simpleOutput,
  shouldOnlyRunDeviceTests: $shouldOnlyRunDeviceTests
  shouldOnlyRunHostTests: $shouldOnlyRunHostTests
  shouldFailFast: $shouldFailFast
  shouldPrintSkipped: $shouldPrintSkipped
  shouldRandomizeTestOrder: $shouldRandomizeTestOrder
  shouldSilenceUnsupported: $shouldSilenceUnsupported
  shouldUpdateIfInBase: $shouldUpdateIfInBase
  slowThreshold: $slowThreshold
>''';
}

/// The parsed parameters passed by our test-running user for evaluation
/// against specific tests available to the current build.
///
/// This handles the fact that users can invoke flags we need to honor in a
/// combinatorial sort of way. For example, consider this invocation:
///
/// ```sh
/// fx test //network //bootloader -d
/// ```
///
/// Here, our developer wants to run all device network tests and all device
/// bootloader tests. To streamline the code that delivers this, we will expand
/// parameters and pretend the developer executed the following two commands:
///
/// ```sh
/// fx test //network -d
/// fx test //bootloader -d
/// ```
/// Each "imagined" command will produce a set of tests which will be combined
/// into a master list of tests we run in one go, with aggregated output.
/// However, the intermediate layer streamlines much of our matching logic.
///
/// [TestsConfig] executes this combinatorial explosion by expanding its
/// parameters into a list of [PermutatedTestFlag] instances.
class TestsConfig {
  final Flags flags;
  final List<String> runnerTokens;
  final TestArguments testArguments;
  final FuchsiaLocator fuchsiaLocator;
  final List<List<MatchableTestName>> testNameGroups;
  TestsConfig({
    @required this.flags,
    @required this.runnerTokens,
    @required this.testArguments,
    @required this.testNameGroups,
    @required this.fuchsiaLocator,
  });

  factory TestsConfig.fromRawArgs({
    @required List<String> rawArgs,
    FuchsiaLocator fuchsiaLocator,
  }) {
    var _testArguments = TestArguments(rawArgs: rawArgs);
    var _testNamesCollector = TestNamesCollector(
      rawArgs: _testArguments.parsedArgs.arguments,
      rawTestNames: _testArguments.parsedArgs.rest,
      fuchsiaLocator: fuchsiaLocator,
    );

    Flags flags = Flags.fromArgResults(_testArguments.parsedArgs);
    return TestsConfig(
      flags: flags,
      fuchsiaLocator: fuchsiaLocator ?? FuchsiaLocator.shared,
      runnerTokens:
          flags.realm != null ? ['--realm-label=${flags.realm}'] : const [],
      testArguments: _testArguments,
      testNameGroups: _testNamesCollector.collect(),
    );
  }

  Iterable<PermutatedTestsConfig> get permutations sync* {
    // Check for having zero `testName` instances, which indicates that the
    // developer wants a wide-open run that includes as many tests as possible
    if (testNameGroups.isEmpty) {
      yield PermutatedTestsConfig(
        flags: flags,
        testNameGroup: null,
      );
      return;
    }
    for (List<MatchableTestName> testNameGroup in testNameGroups) {
      yield PermutatedTestsConfig(
        flags: flags,
        testNameGroup: testNameGroup,
      );
    }
  }

  /// Wrapper around io.ansi.wrapWith which first honors config flags.
  String wrapWith(String value, Iterable<ansi.AnsiCode> codes,
          {bool forScript = false}) =>
      flags.simpleOutput
          ? value
          : ansi.wrapWith(value, codes, forScript: forScript);
}

/// An expanded set of flags passed to `fx test` against which all available
/// tests will be examined.
class PermutatedTestsConfig {
  final List<MatchableTestName> testNameGroup;
  final Flags flags;
  PermutatedTestsConfig({
    @required this.flags,
    @required this.testNameGroup,
  });

  @override
  String toString() {
    var chunks = <String>[
      if (testNameGroup != null) testNameGroup.join(', '),
      if (flags.shouldOnlyRunDeviceTests) '-d',
      if (flags.shouldOnlyRunHostTests) '-h',
    ];
    var chunksStr = chunks.isNotEmpty ? ' ${chunks.join(" ")}' : '';
    return '<PermuatedTestsConfig$chunksStr>';
  }
}
