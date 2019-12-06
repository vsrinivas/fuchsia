// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:io';

import 'package:io/ansi.dart';
import 'package:fxtest/fxtest.dart';
import 'package:meta/meta.dart';

/// Container that holds enough information about a test to execute it and
/// report back on output / events.
class TestBundle {
  /// Wrapper around the individual strings needed to run this test.
  final TestDefinition testDefinition;

  // That which actually launches a process to run the test.
  final TestRunner testRunner;

  /// The directory from which our test assumes it was invoked.
  final String workingDirectory;

  /// Tokens to pass through to individual tests.
  final List<String> extraFlags;

  /// Flag to disable actually running the test.
  final bool isDryRun;

  /// Flag to raise an exception on any test failure which will bubble all the
  /// way up and halt suite execution. Useful when users want "fail-fast"
  /// behavior.
  final bool raiseOnFailure;

  TestBundle(
    this.testDefinition, {
    @required this.workingDirectory,
    this.extraFlags = const [],
    this.raiseOnFailure = false,
    this.isDryRun = false,
    testRunner,
  }) : testRunner = testRunner ?? TestRunner.runner;

  /// Invokes the actual test that this class wraps.
  ///
  /// Returns a stream of test events that send feedback to the user.
  Stream<TestEvent> run() async* {
    CommandTokens commandTokens =
        testDefinition.executionHandle.getInvocationTokens();

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

    String fullCommand = commandTokens.fullCommand;
    yield TestStarted(testName: fullCommand);

    if (isDryRun) {
      yield TestResult.skipped(testName: fullCommand);
      return;
    }
    DateTime start = DateTime.now();
    ProcessResult result = await testRunner.run(
      commandTokens.command,
      commandTokens.args..addAll(extraFlags),
      workingDirectory: workingDirectory,
    );

    yield TestResult(
      testName: fullCommand,
      exitCode: result.exitCode,
      runtime: DateTime.now().difference(start),
      message: result.exitCode == 0
          ? result.stdout
          : _formatError(
              fullCommand,
              result,
            ),
    );
    if (raiseOnFailure && result.exitCode != 0) {
      throw FailFastException();
    }
  }

  String _formatError(String cmd, ProcessResult result) {
    List<String> resultStdout = result.stdout != ''
        ? [...result.stdout.split('\n'), '\n']
            .where((var val) => val != '')
            .cast<String>()
            .toList()
        : [];
    return [
      wrapWith('> $cmd', [red]),
      ...resultStdout,
      result.stderr,
    ].join('\n');
  }
}
