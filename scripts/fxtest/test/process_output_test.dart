// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:async';
import 'dart:io';
import 'package:fxtest/fxtest.dart';
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

      var runner = TestRunner();
      runner.output.listen(addStrings);
      ProcessResult result = await runner.run(
        './test/output_tester.sh',
        [],
        workingDirectory: '.',
      );
      await Future.delayed(Duration(milliseconds: 1));

      expect(strings.length, 3);
      expect(strings[0], 'line 1');
      expect(strings[1], 'line 2');
      expect(strings[2], 'stderr');
      expect(result.stdout, 'line 1\nline 2\n');
    });

    test('when -o is not passed', () async {
      var runner = TestRunner();
      runner.output.listen((val) => null);
      ProcessResult result = await runner.run(
        './test/output_tester.sh',
        [],
        workingDirectory: '.',
      );
      expect(result.stdout, 'line 1\nline 2\n');
    });
  });
}
