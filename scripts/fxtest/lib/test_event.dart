// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

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

class TestInfo extends TestEvent {
  final String message;
  TestInfo(this.message);

  @override
  String toString() => '<TestInfo $message>';
}

class TestStarted extends TestEvent {
  final String testName;

  TestStarted({@required this.testName});

  @override
  String toString() => '<TestStarted $testName>';
}

class BeginningTests extends TestEvent {}

class AllTestsCompleted extends TestEvent {}
