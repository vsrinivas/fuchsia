// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:io';

import 'package:io/ansi.dart';
import 'package:fxtest/fxtest.dart';
import 'package:meta/meta.dart';

class TestRunner {
  /// Entry pulled out of `tests.json`.
  final TestDefinition testDefinition;

  /// The directory from which our test assumes it was invoked.
  final String workingDirectory;

  final bool isDryRun;
  final bool raiseOnFailure;

  TestRunner(
    this.testDefinition, {
    @required this.workingDirectory,
    this.raiseOnFailure = false,
    this.isDryRun = false,
  });

  /// Invokes the actual test that this class wraps.
  ///
  /// Returns a stream of test events that send feedback to the user.
  Stream<TestEvent> run() {
    if (testDefinition.executionHandle.testType == TestType.component) {
      return _runAsComponent(testDefinition.executionHandle.handle);
    }

    if (testDefinition.executionHandle.testType == TestType.suite) {
      return _runAsSuite(testDefinition.executionHandle.handle);
    }

    if (testDefinition.executionHandle.testType == TestType.host) {
      return _runAsBinaryExecution(testDefinition.executionHandle.handle);
    }

    if (testDefinition.executionHandle.testType == TestType.command) {
      return _runAsCommand(testDefinition.executionHandle.handle);
    }

    throw Exception(
      'Failed to determine run context for test:\n$testDefinition',
    );
  }

  /// Handler for test definitions using the "command" keyword.
  ///
  /// Handles tests containing a key like so:
  /// ```json
  /// {"command": ["host_x64/some_binary", "--some-flags"]}
  /// ```
  Stream<TestEvent> _runAsCommand(String fullCommand) async* {
    List<String> fullCommandTokens = fullCommand.split(' ');

    // This should almost always be a binary path assuming the starting point
    // of our build directory
    String command = fullCommandTokens.first;

    // Here we find any necessary flags for invoking this binary
    List<String> remainingCommandTokens =
        fullCommandTokens.sublist(1, fullCommandTokens.length);

    // Currently, some entries in `tests.json` appear due to a bug, and as such,
    // simply with the command ["run", "..."]
    if (command == 'run') {
      yield TestInfo(
        'Warning! Only host tests are expected to use the "command" syntax',
      );
      command = 'fx';
      remainingCommandTokens = ['shell', ...remainingCommandTokens];
    }
    yield* _run(
      command,
      remainingCommandTokens,
    );
  }

  /// Handler for `tests.json` entries containing the `path` key.
  Stream<TestEvent> _runAsBinaryExecution(String path) {
    return _run(path, []);
  }

  /// Handler for `tests.json` entries containing the `packageUrl` key.
  Stream<TestEvent> _runAsComponent(String packageName) {
    List<String> fxCmd = testDefinition.os == 'fuchsia'
        ? ['shell', 'run-test-component']
        : ['run-host-test'];
    return _run('fx', [...fxCmd, packageName]);
  }

  /// Handler for `tests.json` entries containing the `packageUrl` key.
  Stream<TestEvent> _runAsSuite(String packageName) {
    List<String> fxCmd = testDefinition.os == 'fuchsia'
        ? ['shell', 'run-test-suite']
        : ['run-host-test'];
    return _run('fx', [...fxCmd, packageName]);
  }

  Stream<TestEvent> _run(String cmd, List<String> args) async* {
    String fullCmd = '$cmd ${args.join(" ")}'.trim();
    yield TestStarted(testName: fullCmd);

    if (isDryRun) {
      yield TestResult.skipped(testName: fullCmd);
      return;
    }

    DateTime start = DateTime.now();
    ProcessResult result = await Process.run(
      cmd,
      args,
      workingDirectory: workingDirectory,
    );

    yield TestResult(
      testName: fullCmd,
      exitCode: result.exitCode,
      runtime: DateTime.now().difference(start),
      message: result.exitCode == 0
          ? result.stdout
          : _formatError(
              fullCmd,
              result,
            ),
    );

    if (raiseOnFailure && result.exitCode != 0) {
      throw FailFastException();
    }
  }

  String _formatError(String cmd, ProcessResult result) {
    return [
      wrapWith('> $cmd', [red]),
      result.stderr,
    ].join('\n');
  }
}
