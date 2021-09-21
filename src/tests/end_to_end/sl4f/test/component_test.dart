// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TODO(https://fxbug.dev/84961): Fix null safety and remove this language version.
// @dart=2.9

import 'package:sl4f/sl4f.dart' as sl4f;
import 'package:test/test.dart';

const _timeout = Duration(seconds: 60);

void main() {
  sl4f.Sl4f sl4fDriver;

  setUp(() async {
    sl4fDriver = sl4f.Sl4f.fromEnvironment();
    await sl4fDriver.startServer();
  });

  tearDown(() async {
    await sl4fDriver.stopServer();
    sl4fDriver.close();
  });

  group(sl4f.Sl4f, () {
    test('Component search component not running', () async {
      final result = await sl4f.Component(sl4fDriver).search('fake.cmx');
      expect(result, false);
    });

    test('Component search a running component', () async {
      final result = await sl4f.Component(sl4fDriver).search('sl4f.cmx');
      expect(result, true);
    });

    test('Component List running components', () async {
      final result = await sl4f.Component(sl4fDriver).list();
      expect(result.isNotEmpty, true);
    });

    test('tests launcher allows clean for graceful component shutdown',
        () async {
      await sl4fDriver.ssh.run('killall "system_monitor_harvester.cmx"');
      await sl4f.Component(sl4fDriver)
          .launch('system_monitor_harvester', ['--version']);
    });

    test('tests launcher with error', () async {
      final result = await sl4f.Component(sl4fDriver).launch('fake');
      expect(result['Fail'], -1);
    });
  }, timeout: Timeout(_timeout));
}
