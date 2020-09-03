// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:fxtest/fxtest.dart';

/// Concrete flag for an individual test, indicating how it should be executed.
enum TestType {
  command,
  component,
  host,
  suite,

  /// Special tests that start on the host and then interact with a device.
  /// These tests do not always clean up after themselves and thus must be
  /// opted-in to for any given test run.
  e2e,

  /// Catch-all for a test we know `fxtest` has yet to include correct handling
  /// logic. This is not an okay problem, and will raise an error unless a
  /// silencing flag is passed.
  unsupported,

  /// Non-component but on-device tests (an illegal and mostly legacy
  /// configuration).
  unsupportedDeviceTest,
}

const Set<TestType> hostTestTypes = {
  TestType.command,
  TestType.host,
};

const Set<TestType> unsupportedTestTypes = {
  TestType.unsupportedDeviceTest,
  TestType.unsupported,
};

/// Container for all the string primitives required to execute a test.
///
/// Includes every relevant command line argument, flag, and environment
/// variable.
class ExecutionHandle {
  /// Complete string passed to `fx` to execute the test.
  final String handle;

  /// Flags to pass to test runner.
  final List<String> flags;

  /// Name of the operating system which will execute this test. "linux" or "mac"
  /// designate the host, while "fuchsia" designates the target device.
  final String os;

  /// Concrete representation of this class of test.
  final TestType testType;

  /// Environment variables to pass to the spawned [Process] that will actually
  /// execute the test.
  final Map<String, String> environment;

  ExecutionHandle(this.handle, this.os,
      {this.flags = const [], this.testType, this.environment})
      : assert(flags != null);
  ExecutionHandle.command(this.handle, this.os, {this.environment = const {}})
      : testType = TestType.command,
        flags = [];
  ExecutionHandle.component(this.handle, this.os, {this.environment = const {}})
      : testType = TestType.component,
        flags = [];
  ExecutionHandle.e2e(this.handle, this.os, {this.environment = const {}})
      : testType = TestType.e2e,
        flags = [];
  ExecutionHandle.suite(this.handle, this.os,
      {this.flags = const [], this.environment = const {}})
      : testType = TestType.suite;
  ExecutionHandle.host(this.handle, this.os, {this.environment = const {}})
      : testType = TestType.host,
        flags = [];
  ExecutionHandle.unsupportedDeviceTest(this.handle,
      {this.environment = const {}})
      : os = 'fuchsia',
        flags = [],
        testType = TestType.unsupportedDeviceTest;
  const ExecutionHandle.unsupported()
      : handle = '',
        os = '',
        flags = const [],
        environment = const {},
        testType = TestType.unsupported;

  /// Produces the complete list of tokens required to invoke this test.
  ///
  /// This does not account for any extra tokens the user many require - here
  /// we are only considered with vanilla test invocations driven straight from
  /// the definition in the manifest.
  CommandTokens getInvocationTokens(List<String> runnerFlags) {
    if (testType == TestType.command) {
      return _getCommandTokens();
    } else if (testType == TestType.component) {
      return _getComponentTokens(runnerFlags);
    } else if (testType == TestType.host) {
      return _getHostTokens();
    } else if (testType == TestType.suite) {
      return _getSuiteTokens();
    } else if (testType == TestType.e2e) {
      return _getEndToEndTokens();
    }
    return CommandTokens.empty();
  }

  /// Handler for test definitions using the "command" keyword.
  ///
  /// Handles tests containing a key like so:
  /// ```json
  /// {"command": ["host_x64/some_binary", "--some-flags"]}
  /// ```
  CommandTokens _getCommandTokens() {
    List<String> commandTokens = handle.split(' ');

    // Currently, some entries in `tests.json` appear due to a bug, and as such,
    // simply with the command ["run", "..."]. We need to coerce that to its
    // correct syntax, but with a helpful warning.
    if (commandTokens.first == 'run') {
      return CommandTokens(
        ['fx', 'shell', ...commandTokens.sublist(1)],
        warning:
            'Warning! Only host tests are expected to use the "command" syntax. '
            'The test [$commandTokens] did not comply with this expectation.',
      );
    }
    return CommandTokens(commandTokens);
  }

  /// Handler for `tests.json` entries containing the `packageUrl` key ending
  /// in ".cmx".
  CommandTokens _getComponentTokens(List<String> runnerFlags) {
    List<String> subCommand = ['shell', 'run-test-component'] + runnerFlags;
    return CommandTokens(['fx', ...subCommand, handle]);
  }

  /// Handler for `tests.json` entries containing the `packageUrl` key ending
  /// in ".cm".
  CommandTokens _getSuiteTokens() {
    List<String> subCommand = ['shell', 'run-test-suite'];
    return CommandTokens(['fx', ...subCommand, ...flags, handle]);
  }

  /// Handler for `tests.json` entries containing the `path` key.
  CommandTokens _getHostTokens() {
    return CommandTokens([handle]);
  }

  /// Assembles the full invocation command for tests with a device dimension,
  /// but which start on the host machine.
  CommandTokens _getEndToEndTokens() {
    return CommandTokens([handle]);
  }
}
