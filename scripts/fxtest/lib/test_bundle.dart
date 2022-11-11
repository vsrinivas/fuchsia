// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:async';
import 'dart:io';

import 'package:fxtest/fxtest.dart';
import 'package:fxutils/fxutils.dart' as fxutils;
import 'package:path/path.dart' as p;
import 'package:zircon/zircon.dart';

/// Container that holds enough information about a test to execute it and
/// report back on output / events.
class TestBundle {
  /// Wrapper around the individual strings needed to run this test.
  final TestDefinition testDefinition;

  /// That which actually launches a process to run the test.
  final TestRunner testRunner;

  /// Certainty that this [TestBundle] was appropriately matched. A value of 1
  /// means total confidence and a value of 0 means this [TestBundle] should
  /// never have been created.
  final double confidence;

  final DirectoryBuilder directoryBuilder;

  /// Sink for realtime updates from the running test process.
  final Function(String)? _realtimeOutputSink;

  /// Copy of all output, used to send to user when a timeout happens and
  /// [_realtimeOutputSink] is null.
  final StringBuffer _outputBuffer;

  /// Sink for clock updates as a test runs, so the user's expectations can be
  /// managed.
  ///
  /// `Duration` is the test runtime, the first `String` is the test's
  /// invocation command, and the second `String` is any output.
  final Function(Duration, String, String) timeElapsedSink;

  /// The directory from which our test assumes it was invoked.
  final String workingDirectory;

  /// Tokens to pass through to the test runners.
  final List<String> runnerFlags;

  /// Tokens to pass through to individual tests.
  final List<String> extraFlags;

  /// Environment variables to pass to the spawned process that runs our test.
  final Map<String, String> environment;

  /// Flag to disable actually running the test.
  final bool isDryRun;

  /// Flag to raise an exception on any test failure which will bubble all the
  /// way up and halt suite execution. Useful when users want "fail-fast"
  /// behavior.
  final bool raiseOnFailure;

  /// Optional. Path to fx. If provided, it replaces any 'fx' command.
  final String? fxPath;

  /// This will keep track if user doesn't want to restrict logs.
  /// This is temporary till we fully implement new features to decentralize
  /// log restricting list.
  final bool shouldRestrictLogs;

  /// Contains a parallel value specified by the user at invocation time if any.
  /// Supercedes any parallel value stored in the test definition.
  final String? parallelOverride;

  /// Contains a timeout value specified by the user at invocation time if any.
  /// Supercedes any timeout value stored in the test definition.
  final String? timeoutOverride;

  /// Whether or not to use run-test-suite instead of ffx for running v2 tests.
  final bool useRunTestSuite;

  static bool hasDeviceTests(List<TestBundle> testBundles) {
    return testBundles
        .any((e) => !hostTestTypes.contains(e.testDefinition.testType));
  }

  /// Calculate the minimal set of build targets based on tests in [testBundles]
  /// Returns null for a full build.
  static Set<String> calculateMinimalBuildTargets(
      TestsConfig testsConfig, List<TestBundle> testBundles) {
    Set<String> targets = {};
    for (var e in testBundles) {
      switch (e.testDefinition.testType) {
        case TestType.suite:
          String? target = 'updates';
          if (testsConfig.fxEnv.isFeatureEnabled('incremental')) {
            if (e.testDefinition.packageLabel?.isNotEmpty ?? false) {
              target = fxutils.getBuildTarget(e.testDefinition.packageLabel);
            } else if (e.testDefinition.label?.isNotEmpty ?? false) {
              target = fxutils.getBuildTarget(e.testDefinition.label);
            }
          }
          if (target != null) targets.add(target);
          break;
        case TestType.command:
        case TestType.host:
          if (e.testDefinition.path != null) {
            targets.add(e.testDefinition.path!);
          }
          break;
        case TestType.e2e:
          // The presence of an e2e test requires a full build
          return <String>{};
        default:
          break;
      }
    }
    return targets;
  }

  TestBundle(
    this.testDefinition, {
    required this.testRunner,
    required this.timeElapsedSink,
    required this.workingDirectory,
    required this.confidence,
    required this.directoryBuilder,
    required this.parallelOverride,
    required this.timeoutOverride,
    required this.useRunTestSuite,
    this.environment = const <String, String>{},
    this.extraFlags = const [],
    this.fxPath,
    this.isDryRun = false,
    this.raiseOnFailure = false,
    this.shouldRestrictLogs = true,
    this.runnerFlags = const [],
    Function(String)? realtimeOutputSink,
  })  : _realtimeOutputSink = realtimeOutputSink,
        _outputBuffer = StringBuffer() {
    if (confidence <= 0) {
      throw AssertionError('Only confidence values above 0 are allowed');
    }
    if (confidence > 1) {
      throw AssertionError('The maximum valid confidence value is 1');
    }
  }

  factory TestBundle.build({
    required DirectoryBuilder directoryBuilder,
    required TestDefinition testDefinition,
    required TestsConfig testsConfig,
    required Function(Duration, String, String) timeElapsedSink,
    required TestRunner Function(TestsConfig) testRunnerBuilder,
    required String workingDirectory,
    double? confidence,
    Function(String)? realtimeOutputSink,
    String? fxPath,
  }) {
    List<String> _extraFlags = [];
    // for component tests pass test arguments separated by option delimiter(--).
    if (testDefinition.testType == TestType.suite) {
      if (testsConfig.testArguments.passThroughArgs.isNotEmpty) {
        _extraFlags
          ..add('--')
          ..addAll(testsConfig.testArguments.passThroughArgs);
      }
    } else {
      _extraFlags = testsConfig.testArguments.passThroughArgs;
    }

    List<String> runnerFlags =
        List.from(testsConfig.runnerTokens[testDefinition.testType] ?? []);
    final dynRunnerFlags =
        testsConfig.dynamicRunnerTokens[testDefinition.testType] ?? [];
    for (var dynRunnerFlag in dynRunnerFlags) {
      runnerFlags.addAll(dynRunnerFlag.generateTokens());
    }

    return TestBundle(
      testDefinition,
      confidence: confidence ?? 1,
      directoryBuilder: directoryBuilder,
      environment: testsConfig.environment,
      extraFlags: _extraFlags,
      isDryRun: testsConfig.flags.dryRun,
      fxPath: fxPath,
      raiseOnFailure: testsConfig.flags.shouldFailFast,
      shouldRestrictLogs: testsConfig.flags.shouldRestrictLogs,
      runnerFlags: runnerFlags,
      realtimeOutputSink: realtimeOutputSink ?? (String val) => null,
      testRunner: testRunnerBuilder(testsConfig),
      timeElapsedSink: timeElapsedSink,
      workingDirectory: workingDirectory,
      parallelOverride: testsConfig.flags.parallel,
      timeoutOverride: testsConfig.flags.timeout,
      useRunTestSuite: testsConfig.flags.fallbackUseRunTestSuite,
    );
  }

  Function(String) get realtimeOutputSink => (String val) {
        _outputBuffer.writeln(val);
        if (_realtimeOutputSink != null) {
          _realtimeOutputSink!(val);
        }
      };

  /// Invokes the actual test that this class wraps.
  ///
  /// Returns a stream of test events that send feedback to the user.
  Stream<TestEvent> run() async* {
    var testType = testDefinition.testType;
    var executionHandle = testDefinition.createExecutionHandle(
        parallelOverride: parallelOverride,
        timeoutSecondsOverride: timeoutOverride,
        useRunTestSuiteForV2: useRunTestSuite);
    if (testType == TestType.unsupportedDeviceTest) {
      yield UnrunnableTestEvent(executionHandle.handle);
      return;
    }
    var flags = [...runnerFlags];
    // once we implement this feature fully we can remove --restrict-logs flag
    // from fx test.
    if (testDefinition.maxLogSeverity != null && shouldRestrictLogs) {
      switch (testType) {
        case TestType.suite:
          flags.add('--max-severity-logs');
          flags.add(testDefinition.maxLogSeverity!);
          break;
        default:
          break;
      }
    }

    CommandTokens commandTokens = executionHandle.getInvocationTokens(flags);

    // Unparsed tests imply a major problem with `fx test`, so we
    // definitely want to throw an exception
    if (commandTokens.tokens.isEmpty) {
      throw UnrunnableTestException(
        'Failed to determine run context for test:\n$testDefinition',
      );
    }

    // Defer this check to now (as opposed to when the test is compiled), so the
    // feedback can be synced to when tests are executed.
    if (commandTokens.warning != null && commandTokens.warning != '') {
      yield TestInfo(commandTokens.warning!);
    }

    String fullCommandDisplay = commandTokens.fullCommandDisplay(extraFlags);

    yield TestStarted(
      testDefinition: testDefinition,
      command: fullCommandDisplay,
    );

    if (isDryRun) {
      yield TestResult.skipped(testName: fullCommandDisplay);
      return;
    }

    if (testDefinition.isE2E) {
      _createE2eDirectory();
    }
    yield* _runTestWithStopwatch(commandTokens, fullCommandDisplay);
  }

  Stream<TestEvent> _runTestWithStopwatch(
    CommandTokens commandTokens,
    String fullCommandDisplay,
  ) async* {
    var start = DateTime.now();
    Completer timingEvents = Completer();
    Timer.periodic(
      Duration(milliseconds: 100),
      (Timer timer) {
        if (timingEvents.isCompleted) {
          timer.cancel();
        } else {
          timeElapsedSink(
            DateTime.now().difference(start),
            fullCommandDisplay,
            _outputBuffer.toString(),
          );
        }
      },
    );

    TestResult result = await _runTest(commandTokens, fullCommandDisplay);
    if (!timingEvents.isCompleted) {
      timingEvents.complete();
    }
    // ffx returns a ZX_ERR_UNAVAILABLE code if it could not connect to test
    // manager. This indicates some issue with the setup and isn't retriable.
    if (result.exitCode == -ZX.ERR_UNAVAILABLE) {
      yield FatalError('Could not run ${testDefinition.name}');
      throw TestFrameworkUnavailableError();
    } else {
      yield result;
    }
    if (raiseOnFailure && result.exitCode != 0) {
      throw FailFastException();
    }
  }

  Future<TestResult> _runTest(
    CommandTokens commandTokens,
    String fullCommandDisplay,
  ) async {
    var start = DateTime.now();
    testRunner.output.listen(realtimeOutputSink);
    ProcessResult result = await testRunner.run(
      _addFxPath(commandTokens.command),
      commandTokens.args..addAll(extraFlags),
      environment: environment,
      workingDirectory: workingDirectory,
    );
    return TestResult(
      testName: testDefinition.name,
      command: fullCommandDisplay,
      exitCode: result.exitCode,
      runtime: DateTime.now().difference(start),
      message: result.exitCode == 0
          ? result.stdout
          : _formatError(
              fullCommandDisplay,
              result,
            ),
    );
  }

  String _addFxPath(String cmd) {
    return cmd == 'fx' && fxPath != null ? fxPath! : cmd;
  }

  String _formatError(String cmd, ProcessResult result) {
    List<String> resultStdout = result.stdout != ''
        ? [...result.stdout.split('\n'), '\n']
            .where((var val) => val != '')
            .cast<String>()
            .toList()
        : [];
    return [
      ...resultStdout,
      result.stderr,
    ].join('\n');
  }

  void _createE2eDirectory() {
    if (!environment.containsKey('FUCHSIA_TEST_OUTDIR') ||
        environment['FUCHSIA_TEST_OUTDIR'] == null ||
        environment['FUCHSIA_TEST_OUTDIR'] == '') {
      throw Exception(
          '`FUCHSIA_TEST_OUTDIR` environment variable must be set when running e2e tests.');
    }
    directoryBuilder(
      p.join(
        environment['FUCHSIA_TEST_OUTDIR']!,
        testDefinition.name,
      ),
      recursive: true,
    );
  }

  @override
  String toString() => [
        '<TestBundle',
        '  testName: ${testDefinition.name}',
        '  testType: ${testDefinition.testType}',
        '  extraFlags: $extraFlags',
        '  runnerFlags: $runnerFlags',
        '/>',
      ].join('\n');
}
