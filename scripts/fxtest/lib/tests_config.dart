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
  final String minSeverityLogs;
  final bool allOutput;
  final bool shouldRebuild;

  /// Extra tokens to be passed through to individual tests.
  final bool infoOnly;
  final MatchLength matchLength;
  final bool shouldFailFast;
  final bool simpleOutput;
  final bool shouldOnlyRunDeviceTests;
  final bool shouldOnlyRunHostTests;
  final bool shouldRestrictLogs;
  final bool shouldPrintSkipped;
  final bool shouldRandomizeTestOrder;
  final bool shouldSilenceUnsupported;
  final bool shouldUpdateIfInBase;
  final bool shouldUsePackageHash;
  final int slowThreshold;
  Flags({
    this.dryRun = false,
    this.isVerbose = false,
    this.limit = 0,
    this.realm,
    this.minSeverityLogs,
    this.allOutput = false,
    this.infoOnly = false,
    this.matchLength = MatchLength.partial,
    this.simpleOutput = false,
    this.shouldFailFast = false,
    this.shouldOnlyRunDeviceTests = false,
    this.shouldOnlyRunHostTests = false,
    this.shouldRestrictLogs = false,
    this.shouldPrintSkipped = false,
    this.shouldRandomizeTestOrder = false,
    this.shouldRebuild = true,
    this.shouldSilenceUnsupported = false,
    this.shouldUpdateIfInBase = true,
    this.shouldUsePackageHash = true,
    this.slowThreshold = 0,
  });

  factory Flags.fromArgResults(ArgResults argResults) {
    return Flags(
      allOutput: argResults['output'],
      dryRun: argResults['info'] || argResults['dry'],
      infoOnly: argResults['info'],
      isVerbose: argResults['verbose'] || argResults['output'],
      limit: int.parse(argResults['limit'] ?? '0'),
      matchLength: argResults['exact'] ? MatchLength.full : MatchLength.partial,
      realm: argResults['realm'],
      minSeverityLogs: argResults['min-severity-logs'],
      simpleOutput: argResults['simple'],
      shouldFailFast: argResults['fail'],
      shouldOnlyRunDeviceTests: argResults['device'],
      shouldOnlyRunHostTests: argResults['host'],
      shouldRestrictLogs: argResults['restrict-logs'],
      shouldPrintSkipped: argResults['skipped'],

      // True (aka, yes rebuild) if `no-build` is missing or set to `False`
      shouldRebuild: (!argResults['info'] && !argResults['dry']) &&
          (argResults['build'] == null || argResults['build']),
      shouldRandomizeTestOrder: argResults['random'],
      shouldSilenceUnsupported: argResults['silenceunsupported'],
      shouldUpdateIfInBase: argResults['updateifinbase'],
      shouldUsePackageHash: argResults['use-package-hash'],
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
  info: $infoOnly,
  matchLength: ${matchLength.toString()},
  shouldFailFast: $shouldFailFast
  simpleOutput: $simpleOutput,
  shouldOnlyRunDeviceTests: $shouldOnlyRunDeviceTests
  shouldOnlyRunHostTests: $shouldOnlyRunHostTests
  shouldRestrictLogs: $shouldRestrictLogs
  shouldPrintSkipped: $shouldPrintSkipped
  shouldRandomizeTestOrder: $shouldRandomizeTestOrder
  shouldSilenceUnsupported: $shouldSilenceUnsupported
  shouldUpdateIfInBase: $shouldUpdateIfInBase
  shouldUsePackageHash: $shouldUsePackageHash
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
  final List<List<MatchableArgument>> testArgumentGroups;
  TestsConfig({
    @required this.flags,
    @required this.runnerTokens,
    @required this.testArguments,
    @required this.testArgumentGroups,
    @required this.fuchsiaLocator,
  });

  factory TestsConfig.fromRawArgs({
    @required List<String> rawArgs,
    FuchsiaLocator fuchsiaLocator,
  }) {
    var _testArguments = TestArguments(rawArgs: rawArgs);
    var _testArgumentsCollector = TestNamesCollector(
      rawArgs: _testArguments.parsedArgs.arguments,
      rawTestNames: _testArguments.parsedArgs.rest,
      fuchsiaLocator: fuchsiaLocator,
    );
    Flags flags = Flags.fromArgResults(_testArguments.parsedArgs);

    var runnerTokens = <String>[];
    if (flags.realm != null) {
      runnerTokens.add('--realm-label=${flags.realm}');
    }
    if (flags.shouldRestrictLogs) {
      runnerTokens.add('--restrict-logs');
    }
    if (flags.minSeverityLogs != null) {
      runnerTokens.add('--min-severity-logs=${flags.minSeverityLogs}');
    }
    return TestsConfig(
      flags: flags,
      fuchsiaLocator: fuchsiaLocator ?? FuchsiaLocator.shared,
      runnerTokens: runnerTokens,
      testArguments: _testArguments,
      testArgumentGroups: _testArgumentsCollector.collect(),
    );
  }

  Iterable<PermutatedTestsConfig> get permutations sync* {
    // Check for having zero `testName` instances, which indicates that the
    // developer wants a wide-open run that includes as many tests as possible
    if (testArgumentGroups.isEmpty) {
      yield PermutatedTestsConfig(
        flags: flags,
        testNameGroup: null,
      );
      return;
    }
    for (List<MatchableArgument> testNameGroup in testArgumentGroups) {
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
  final List<MatchableArgument> testNameGroup;
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
