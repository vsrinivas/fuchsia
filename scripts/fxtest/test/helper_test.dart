// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:async/async.dart';
import 'package:test/test.dart';
import 'helpers.dart';

// But who will test the testers?
void main() {
  group('ScriptedTestRunner correctly mimics TestRunner', () {
    test('with simple output', () async {
      var runner = ScriptedTestRunner(scriptedOutput: [
        Output('test'),
        Output('test 2'),
        ErrOutput('test 3'),
      ]);
      var processResult = await runner.easyRun();
      expect(processResult.stdout, 'test\ntest 2\n');
      expect(processResult.stderr, 'test 3\n');
    });

    test('with simple output and delays', () async {
      var runner = ScriptedTestRunner(scriptedOutput: [
        Output('test'),
        Output('test 2'),
        Duration(milliseconds: 1),
        ErrOutput('test 3'),
      ]);
      var processResult = await runner.easyRun();
      var queue = StreamQueue(runner.output);
      await expectLater(queue, emits('test'));
      await expectLater(queue, emits('test 2'));
      await expectLater(queue, emits('test 3'));
      expect(processResult.stdout, 'test\ntest 2\n');
      expect(processResult.stderr, 'test 3\n');
    });
  });
}
