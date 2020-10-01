// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:fxtest/test_definition.dart';
import 'package:meta/meta.dart';

/// Base class for our test suite stream which keeps our output in sync with
/// ongoing test progress.
abstract class TestEvent {}

/// An error so bad that test suite execution cannot continue.
class FatalError extends TestEvent {
  final String message;
  FatalError(this.message);
}

class TestResult extends TestEvent {
  /// The standard system exit code for a test.
  final int exitCode;

  /// Friendly string to print indicating this test.
  ///
  /// It is valid to put the whole invocation command here, as that will allow
  /// developers to most-easily directly target their specific test, should they
  /// so choose.
  final String testName;

  /// Whether the test was actually executed.
  ///
  /// Will be `true` if the test was run and `false` if the test was skipped.
  final bool isDryRun;

  /// Explanatory string accompanying the test result - likely only supplied on
  /// test failures.
  final String message;

  /// How long just this test took to execute - does not count any (hopefully
  /// trivial) overhead time from this script.
  final Duration runtime;

  TestResult({
    @required this.testName,
    @required this.exitCode,
    @required this.message,
    @required this.runtime,
  }) : isDryRun = false;

  TestResult.skipped({
    @required this.testName,
  })  : isDryRun = true,
        exitCode = 0,
        runtime = Duration(),
        message = null;

  bool get isSuccess => exitCode == 0;

  @override
  String toString() => '<TestResult $testName: $exitCode>';
}

/// Various updates to display to the user in real time.
class TestInfo extends TestEvent {
  final String message;

  /// Determines the presence or absence of a cosmetic spacer line.
  ///
  /// Debug statements often require this to de-clutter output, but, for example,
  /// raw stack traces are pre-formatted and thus must not receive any additional
  /// cosmetic treatment.
  final bool requiresPadding;
  TestInfo(this.message, {this.requiresPadding = true});

  @override
  String toString() => '<TestInfo $message>';
}

/// An event indicating that a test was legacy and not runnable.
class UnrunnableTestEvent extends TestEvent {
  final String testName;

  UnrunnableTestEvent(this.testName);
}

/// Specialized version of [TestInfo] that users can opt-in to via the "-o"
/// flag.
class TestOutputEvent extends TestInfo {
  TestOutputEvent(String message, {bool requiresPadding = false})
      : super(
          message,
          requiresPadding: requiresPadding,
        );
  @override
  String toString() => '<TestOutputEvent $message>';
}

class TestStarted extends TestEvent {
  final TestDefinition testDefinition;
  final String testName;

  TestStarted({
    @required this.testDefinition,
    @required this.testName,
  });

  @override
  String toString() => '<TestStarted $testName>';
}

/// Signifies to output formatters that we have entered this phase.
class BeginningTests extends TestEvent {}

/// Signifies to output formatters that we have entered this phase.
class AllTestsCompleted extends TestEvent {}

class TimeElapsedEvent extends TestEvent {
  final Duration timeElapsed;
  final String command;
  final List<String> output;
  TimeElapsedEvent(this.timeElapsed, this.command, String _output)
      : output = _output.split('\n');

  @override
  String toString() => '<TimeElapsedEvent $timeElapsed :: $command />';
}

/// Signifies to output formatters that we have entered this phase.
class GeneratingHintsEvent extends TestEvent {}
