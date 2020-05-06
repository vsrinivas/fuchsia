// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:async';
import 'dart:io';

import 'package:io/ansi.dart';
import 'package:fxtest/fxtest.dart';
import 'package:meta/meta.dart';

/// Container that holds enough information about a test to execute it and
/// report back on output / events.
class TestBundle {
  /// Wrapper around the individual strings needed to run this test.
  final TestDefinition testDefinition;

  /// That which actually launches a process to run the test.
  final TestRunner testRunner;

  /// Sink for realtime updates from the running test process.
  final Function(String) _realtimeOutputSink;

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

  /// Flag to disable actually running the test.
  final bool isDryRun;

  /// Flag to raise an exception on any test failure which will bubble all the
  /// way up and halt suite execution. Useful when users want "fail-fast"
  /// behavior.
  final bool raiseOnFailure;

  /// Optional. Trailing substring of the path to fx. If provided, used to detect
  /// and condense full fx paths in output.
  final String fxSuffix;

  TestBundle(
    this.testDefinition, {
    @required this.testRunner,
    @required this.timeElapsedSink,
    @required this.workingDirectory,
    this.extraFlags = const [],
    this.fxSuffix,
    this.isDryRun = false,
    this.raiseOnFailure = false,
    this.runnerFlags = const [],
    Function(String) realtimeOutputSink,
  })  : _realtimeOutputSink = realtimeOutputSink,
        _outputBuffer = StringBuffer() {
    if (testRunner == null) {
      throw AssertionError('`testRunnerBuilder` must not equal `null`');
    }
  }

  factory TestBundle.build({
    @required TestDefinition testDefinition,
    @required TestsConfig testsConfig,
    @required Function(Duration, String, String) timeElapsedSink,
    @required TestRunner Function(TestsConfig) testRunnerBuilder,
    @required String workingDirectory,
    Function(String) realtimeOutputSink,
    String fxSuffix,
  }) =>
      TestBundle(
        testDefinition,
        extraFlags: testsConfig.testArguments.passThroughArgs,
        isDryRun: testsConfig.flags.dryRun,
        fxSuffix: fxSuffix,
        raiseOnFailure: testsConfig.flags.shouldFailFast,
        runnerFlags: testsConfig.runnerTokens,
        realtimeOutputSink: realtimeOutputSink ?? (String val) => null,
        testRunner: testRunnerBuilder(testsConfig),
        timeElapsedSink: timeElapsedSink,
        workingDirectory: workingDirectory,
      );

  Function(String) get realtimeOutputSink => (String val) {
        _outputBuffer.writeln(val);
        if (_realtimeOutputSink != null) {
          _realtimeOutputSink(val);
        }
      };

  /// Invokes the actual test that this class wraps.
  ///
  /// Returns a stream of test events that send feedback to the user.
  Stream<TestEvent> run() async* {
    var testType = testDefinition.executionHandle.testType;
    if (testType == TestType.unsupportedDeviceTest) {
      var greyTestName =
          wrapWith(testDefinition.executionHandle.handle, [styleBold]);
      yield TestInfo(
        'Skipping unrunnable legacy test: "$greyTestName". '
        'All device tests must be component-tests, but this is a binary',
      );
      return;
    }

    CommandTokens commandTokens =
        testDefinition.executionHandle.getInvocationTokens(runnerFlags);

    // Unparsed tests imply a major problem with `fx test`, so we
    // definitely want to throw an exception
    if (commandTokens.tokens == null || commandTokens.tokens.isEmpty) {
      throw UnrunnableTestException(
        'Failed to determine run context for test:\n$testDefinition',
      );
    }

    // Defer this check to now (as opposed to when the test is compiled), so the
    // feedback can be synced to when tests are executed.
    if (commandTokens.warning != null && commandTokens.warning != '') {
      yield TestInfo(commandTokens.warning);
    }

    String fullCommandDisplay = commandTokens.fullCommandDisplay(fxSuffix);

    yield TestStarted(
      testDefinition: testDefinition,
      testName: fullCommandDisplay,
    );

    if (isDryRun) {
      yield TestResult.skipped(testName: fullCommandDisplay);
      return;
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
    yield result;
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
      commandTokens.command,
      commandTokens.args..addAll(extraFlags),
      workingDirectory: workingDirectory,
    );
    return TestResult(
      testName: fullCommandDisplay,
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

  @override
  String toString() => [
        '<TestBundle',
        '  testName: ${testDefinition.name}',
        '  testType: ${testDefinition.executionHandle.testType}',
        '  extraFlags: $extraFlags',
        '  runnerFlags: $runnerFlags',
        '/>',
      ].join('\n');
}
