// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:args/args.dart';
import 'package:fxtest/fxtest.dart';
import 'package:fxutils/fxutils.dart';
import 'package:io/ansi.dart' as ansi;
import 'package:path/path.dart' as p;

// ignore: prefer_generic_function_type_aliases
typedef String Stylizer(String value, Iterable<ansi.AnsiCode> codes,
    {bool forScript});

// ignore: prefer_generic_function_type_aliases
typedef void DirectoryBuilder(String path, {required bool recursive});

/// Simple class to hold shared parameters.
class Flags {
  final bool dryRun;
  final bool isVerbose;

  /// The maximum number of tests to run. If 0, all tests will be executed.
  final int limit;

  /// The realm name to run the test inside of. If null, a random name is used.
  final String? realm;
  final String? minSeverityLogs;
  final bool allOutput;
  final bool shouldRebuild;

  final bool e2e;
  final int fuzzyThreshold;
  final bool infoOnly;
  final String? logPath;
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
  final String? timeout;

  // flags for v2 tests
  final List<String>? testFilter;
  final String? count;
  final String? parallel;
  final String? ffxOutputDirectory;
  final bool showFullMonikerInLogs;
  final bool runDisabledTests;
  final bool fallbackUseRunTestSuite;

  Flags({
    this.dryRun = false,
    this.isVerbose = false,
    this.limit = 0,
    this.realm,
    this.minSeverityLogs,
    this.allOutput = false,
    this.e2e = false,
    this.fuzzyThreshold = 3,
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
    this.timeout,
    this.testFilter,
    this.count,
    this.parallel,
    this.ffxOutputDirectory,
    this.showFullMonikerInLogs = false,
    this.runDisabledTests = false,
    this.fallbackUseRunTestSuite = false,
  });

  factory Flags.fromArgResults(ArgResults argResults) {
    int.parse(argResults['count'] ?? '0');
    int.parse(argResults['timeout'] ?? '0');
    int.parse(argResults['parallel'] ?? '0');
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
        matchLength:
            argResults['exact'] ? MatchLength.full : MatchLength.partial,
        realm: argResults['realm'],
        minSeverityLogs: argResults['min-severity-logs'],
        simpleOutput: argResults['simple'],
        shouldFailFast: argResults['fail'],
        shouldLog: argResults['log'],
        shouldOnlyRunDeviceTests: argResults['device'],
        shouldOnlyRunHostTests: argResults['host'],
        shouldRestrictLogs: argResults['restrict-logs'],
        shouldPrintSkipped: argResults['skipped'],
        testFilter: argResults['test-filter'],

        // True (aka, yes rebuild) if `no-build` is missing or set to `False`
        shouldRebuild: (!argResults['info'] && !argResults['dry']) &&
            (argResults['build'] == null || argResults['build']),
        shouldRandomizeTestOrder: argResults['random'],
        shouldSilenceUnsupported: argResults['silenceunsupported'],
        shouldUpdateIfInBase: argResults['updateifinbase'],
        shouldUsePackageHash: argResults['use-package-hash'],
        slowThreshold: int.parse(argResults['slow'] ?? '0'),
        timeout: argResults['timeout'],
        count: argResults['count'],
        parallel: argResults['parallel'],
        ffxOutputDirectory: argResults['ffx-output-directory'],
        showFullMonikerInLogs: argResults['show-full-moniker-in-logs'],
        runDisabledTests: argResults['also-run-disabled-tests'],
        fallbackUseRunTestSuite: argResults['use-run-test-suite']);
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
  testFilter: $testFilter
  count: $count
  parallel: $parallel
  showFullMonikerInLogs: $showFullMonikerInLogs
  ffxOutputDirectory: $ffxOutputDirectory
  fallbackUseRunTestSuite: $fallbackUseRunTestSuite
>''';
}

/// Tokens generated at invocation time, for example, to provide tests with
/// isolated resources.
abstract class DynamicRunnerToken {
  List<String> generateTokens();
}

/// Tokens for ffx test that produce an isolated output directory for each
/// invocation of ffx test run.
class FfxOutputDirectoryToken implements DynamicRunnerToken {
  final String rootDirectory;
  int _nextDirectoryId;

  FfxOutputDirectoryToken(this.rootDirectory) : _nextDirectoryId = 0;

  List<String> generateTokens() {
    final directoryName = p.join(rootDirectory, '$_nextDirectoryId');
    _nextDirectoryId += 1;
    return ['--output-directory', directoryName];
  }
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
  final Map<TestType, List<String>> runnerTokens;
  final Map<TestType, List<DynamicRunnerToken>> dynamicRunnerTokens;
  final TestArguments testArguments;
  final IFxEnv fxEnv;
  final List<List<MatchableArgument>> testArgumentGroups;
  TestsConfig({
    required this.flags,
    required this.runnerTokens,
    required this.dynamicRunnerTokens,
    required this.testArguments,
    required this.testArgumentGroups,
    required this.fxEnv,
  });

  factory TestsConfig.fromRawArgs({
    required IFxEnv fxEnv,
    required List<String> rawArgs,
    Map<String, String?>? defaultRawArgs,
  }) {
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

    var v1runnerTokens = <String>[];
    if (flags.realm != null) {
      v1runnerTokens.add('--realm-label=${flags.realm}');
    }
    if (flags.minSeverityLogs != null) {
      v1runnerTokens.add('--min-severity-logs=${flags.minSeverityLogs}');
    }

    var v2runnerTokens = <String>[];
    var v2dynamicTokens = <DynamicRunnerToken>[];

    if (flags.testFilter != null) {
      for (var filter in flags.testFilter!) {
        v2runnerTokens
          ..add('--test-filter')
          ..add(filter);
      }
    }

    if (flags.count != null) {
      v2runnerTokens
        ..add('--count')
        ..add(flags.count!);
    }
    if (flags.shouldFailFast) {
      // Since we partially delegate running tests multiple times to the v2 runner.
      // we also need to instruct it to fail fast on failure too.
      v2runnerTokens
        ..add('--stop-after-failures')
        ..add('1');
    }
    // We do not add the parallel or timeout options here, as they may also be
    // specified via test spec. Instead, it is added later.
    if (flags.runDisabledTests && flags.fallbackUseRunTestSuite) {
      v2runnerTokens.add('--also-run-disabled-tests');
    } else if (flags.runDisabledTests) {
      v2runnerTokens.add('--run-disabled');
    }
    if (flags.minSeverityLogs != null) {
      v2runnerTokens
        ..add('--min-severity-logs')
        ..add(flags.minSeverityLogs.toString());
    }
    var ffxOutputDirectory = flags.ffxOutputDirectory;
    if (ffxOutputDirectory != null && !flags.fallbackUseRunTestSuite) {
      v2dynamicTokens.add(FfxOutputDirectoryToken(ffxOutputDirectory));
    } else if (!flags.fallbackUseRunTestSuite) {
      v2runnerTokens.add('--disable-output-directory');
    }

    if (flags.showFullMonikerInLogs && !flags.fallbackUseRunTestSuite) {
      v2runnerTokens.add('--show-full-moniker-in-logs');
    }

    return TestsConfig(
      flags: flags,
      fxEnv: fxEnv,
      runnerTokens: {
        TestType.component: v1runnerTokens,
        TestType.suite: v2runnerTokens
      },
      dynamicRunnerTokens: {
        TestType.component: [],
        TestType.suite: v2dynamicTokens,
      },
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
        true, () => ansi.wrapWith(value, codes, forScript: forScript)!);
  }

  void _maybeAddEnv(Map<String, String> map, String envName,
      [String? newEnvName]) {
    String? envValue = fxEnv.getEnv(envName);
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
  final List<MatchableArgument>? testNameGroup;
  final Flags flags;
  PermutatedTestsConfig({
    required this.flags,
    required this.testNameGroup,
  });

  @override
  String toString() {
    var chunks = <String>[
      if (testNameGroup != null) testNameGroup!.join(', '),
      if (flags.shouldOnlyRunDeviceTests) '-d',
      if (flags.shouldOnlyRunHostTests) '-h',
    ];
    var chunksStr = chunks.isNotEmpty ? ' ${chunks.join(" ")}' : '';
    return '<PermuatedTestsConfig$chunksStr>';
  }
}
