// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:fxtest/fxtest.dart';

/// Concrete flag for an individual test, indicating how it should be executed.
///
/// Note that [unsupported] is included as a bucket for tests we have failed
/// to account for. It is not an acceptable place for tests to end up. Should
/// any tests find their way here, an exception will be raised that will halt
/// test execution entirely (but which can be silenced with a flag).
enum TestType { command, component, host, suite, unsupported }

class ExecutionHandle {
  final String handle;
  final String os;
  final TestType testType;
  ExecutionHandle(this.handle, this.os, {this.testType});
  ExecutionHandle.command(this.handle, this.os) : testType = TestType.command;
  ExecutionHandle.component(this.handle, this.os)
      : testType = TestType.component;
  ExecutionHandle.suite(this.handle, this.os) : testType = TestType.suite;
  ExecutionHandle.host(this.handle, this.os) : testType = TestType.host;
  ExecutionHandle.unsupported()
      : handle = '',
        os = '',
        testType = TestType.unsupported;

  bool get isUnsupported => testType == TestType.unsupported;

  /// Produces the complete list of tokens required to invoke this test.
  ///
  /// This does not account for any extra tokens the user many require - here
  /// we are only considered with vanilla test invocations driven straight from
  /// the definition in the manifest.
  CommandTokens getInvocationTokens() {
    if (testType == TestType.command) {
      return _getCommandTokens();
    } else if (testType == TestType.component) {
      return _getComponenTokens();
    } else if (testType == TestType.host) {
      return _getHostTokens();
    } else if (testType == TestType.suite) {
      return _getSuiteTokens();
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
  CommandTokens _getComponenTokens() {
    List<String> subCommand =
        os == 'fuchsia' ? ['shell', 'run-test-component'] : ['run-host-test'];
    return CommandTokens(['fx', ...subCommand, handle]);
  }

  /// Handler for `tests.json` entries containing the `packageUrl` key ending
  /// in ".cm".
  CommandTokens _getSuiteTokens() {
    List<String> subCommand =
        os == 'fuchsia' ? ['shell', 'run-test-suite'] : ['run-host-test'];
    return CommandTokens(['fx', ...subCommand, handle]);
  }

  /// Handler for `tests.json` entries containing the `path` key.
  CommandTokens _getHostTokens() {
    return CommandTokens([handle]);
  }
}
