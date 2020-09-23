// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:args/args.dart';
import 'package:fxtest/fxtest.dart';
import 'package:fxutils/fxutils.dart';
import 'package:io/ansi.dart' as ansi;
import 'package:meta/meta.dart';

// ignore: prefer_generic_function_type_aliases
typedef String Stylizer(String value, Iterable<ansi.AnsiCode> codes,
    {bool forScript});

// ignore: prefer_generic_function_type_aliases
typedef void DirectoryBuilder(String path, {bool recursive});

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

  final bool e2e;
  final int fuzzyThreshold;
  final bool infoOnly;
  final String logPath;
  final MatchLength matchLength;
  final bool onlyE2e;
  final bool shouldFailFast;
  final bool shouldLog;
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
    this.e2e = false,
    this.fuzzyThreshold,
    this.infoOnly = false,
    this.logPath,
    this.matchLength = MatchLength.partial,
    this.onlyE2e = false,
    this.simpleOutput = false,
    this.shouldLog = true,
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
      e2e: argResults['e2e'] || argResults['only-e2e'],
      fuzzyThreshold: int.parse(argResults['fuzzy']),
      onlyE2e: argResults['only-e2e'],
      infoOnly: argResults['info'],
      isVerbose: argResults['verbose'] || argResults['output'],
      limit: int.parse(argResults['limit'] ?? '0'),
      logPath: argResults['logpath'],
      matchLength: argResults['exact'] ? MatchLength.full : MatchLength.partial,
      realm: argResults['realm'],
      minSeverityLogs: argResults['min-severity-logs'],
      simpleOutput: argResults['simple'],
      shouldFailFast: argResults['fail'],
      shouldLog: argResults['log'],
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
  allOutput: $allOutput,
  dryRun: $dryRun
  fuzzyThreshold: $fuzzyThreshold
  isVerbose: $isVerbose
  e2e: $e2e,
  info: $infoOnly,
  limit: $limit
  logPath: $logPath,
  matchLength: ${matchLength.toString()},
  realm: $realm
  min-severity-logs: $minSeverityLogs,
  shouldFailFast: $shouldFailFast
  simpleOutput: $simpleOutput,
  shouldFailFast: $shouldFailFast
  shouldLog: $shouldLog
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
  final IFxEnv fxEnv;
  final List<List<MatchableArgument>> testArgumentGroups;
  TestsConfig({
    @required this.flags,
    @required this.runnerTokens,
    @required this.testArguments,
    @required this.testArgumentGroups,
    @required this.fxEnv,
  });

  factory TestsConfig.fromRawArgs({
    @required IFxEnv fxEnv,
    @required List<String> rawArgs,
    Map<String, String> defaultRawArgs,
  }) {
    if (fxEnv == null) {
      throw Exception('TestsConfig requires an FxEnv instance');
    }
    var _testArguments = TestArguments(
      parser: fxTestArgParser,
      rawArgs: rawArgs,
      defaultRawArgs: defaultRawArgs,
    );
    var _testArgumentsCollector = TestNamesCollector(
      rawArgs: _testArguments.parsedArgs.arguments,
      rawTestNames: _testArguments.parsedArgs.rest,
      relativeCwd: fxEnv.relativeCwd,
    );
    Flags flags = Flags.fromArgResults(_testArguments.parsedArgs);

    var runnerTokens = <String>[];
    if (flags.realm != null) {
      runnerTokens.add('--realm-label=${flags.realm}');
    }
    if (flags.minSeverityLogs != null) {
      runnerTokens.add('--min-severity-logs=${flags.minSeverityLogs}');
    }

    return TestsConfig(
      flags: flags,
      fxEnv: fxEnv,
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
      {bool forScript = false}) {
    // TODO(fxbug.dev/53267): Remove the override once terminal detection works inside
    // tmux.
    if (flags.simpleOutput) {
      return value;
    }

    return ansi.overrideAnsiOutput(
        true, () => ansi.wrapWith(value, codes, forScript: forScript));
  }

  void _maybeAddEnv(Map<String, String> map, String envName,
      [String newEnvName]) {
    String envValue = fxEnv.getEnv(envName);
    if (envValue != null && envValue.isNotEmpty) {
      map[newEnvName ?? envName] = envValue;
    }
  }

  /// Environment variables to pass to the spawned process that runs our test.
  Map<String, String> get environment {
    if (flags.e2e) {
      Map<String, String> result = {};
      _maybeAddEnv(result, 'FUCHSIA_DEVICE_ADDR');
      _maybeAddEnv(result, 'FUCHSIA_SSH_KEY');
      _maybeAddEnv(result, 'FUCHSIA_SSH_PORT');
      _maybeAddEnv(result, 'FUCHSIA_TEST_OUTDIR');
      _maybeAddEnv(result, 'SL4F_HTTP_PORT');
      // Legacy key
      _maybeAddEnv(result, 'FUCHSIA_DEVICE_ADDR', 'FUCHSIA_IPV4_ADDR');
      return result;
    } else {
      return const {};
    }
  }
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
