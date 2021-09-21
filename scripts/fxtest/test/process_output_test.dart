// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TODO(https://fxbug.dev/84961): Fix null safety and remove this language version.
// @dart=2.9

import 'dart:io';

import 'package:fxtest/fxtest.dart';
import 'package:fxutils/fxutils.dart';
import 'package:test/test.dart';

// Note: These tests pass locally (when executed by `pub run test`), but not
// when built by GN, because of their dependency on `output_tester.sh`.
// For this reason, they are commented out in the BUILD.gn file. Ideally, a
// solution here will be worked out and the directive in BUILD.gn can be
// uncommented.

void main() {
  group('test output is routed correctly', () {
    test('when -o is passed', () async {
      var strings = <String>[];
      void addStrings(String s) {
        strings.add(s);
      }

      var startProcess = mockStartProcess(createOutputTester());
      var runner = TestRunner(startProcess: startProcess);
      runner.output.listen(addStrings);
      var result = await runner.run(
        './test/output_tester.sh',
        [],
        workingDirectory: '.',
      );

      expect(strings.length, 3);
      expect(strings[0], 'line 1');
      expect(strings[1], 'line 2');
      expect(strings[2], 'stderr');
      expect(result.stdout, 'line 1\nline 2\n');
    });

    test('when -o is not passed', () async {
      var startProcess = mockStartProcess(createOutputTester());
      var runner = TestRunner(startProcess: startProcess);
      runner.output.listen((_) {});
      var result = await runner.run(
        './test/output_tester.sh',
        [],
        workingDirectory: '.',
      );
      expect(result.stdout, 'line 1\nline 2\n');
    });
  });
}

/// Creates an output tester mock process with hardcoded output.
Process createOutputTester() => MockProcess.raw(
      stdout: 'line 1\nline 2\n',
      stderr: 'stderr\n',
    );
