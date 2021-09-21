// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TODO(https://fxbug.dev/84961): Fix null safety and remove this language version.
// @dart=2.9

import 'package:async/async.dart';
import 'package:test/test.dart';
import 'fake_fx_env.dart';
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

  group('FakeFxEnv', () {
    test('should return correct fuchsiaDir', () {
      expect(FakeFxEnv.shared.fuchsiaDir, '/root/fuchsia');
    });
    test('should return correct outputDir', () {
      expect(FakeFxEnv.shared.outputDir, '/root/fuchsia/out/default');
    });
    test('should return correct relativeCwd', () {
      expect(
        FakeFxEnv(cwd: '/root/fuchsia/out/default/host_x64/gen').relativeCwd,
        'host_x64/gen',
      );
    });
  });
}
