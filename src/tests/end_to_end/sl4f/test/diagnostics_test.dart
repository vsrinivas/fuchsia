// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TODO(https://fxbug.dev/84961): Fix null safety and remove this language version.
// @dart=2.9

import 'dart:io';

import 'package:sl4f/sl4f.dart' as sl4f;
import 'package:test/test.dart';

const _timeout = Duration(seconds: 60);

void main() {
  sl4f.Sl4f sl4fDriver;
  sl4f.Dump dump;
  sl4f.Diagnostics diagnostics;
  Directory dumpDir;

  setUp(() async {
    sl4fDriver = sl4f.Sl4f.fromEnvironment();
    await sl4fDriver.startServer();

    dumpDir = await Directory.systemTemp.createTemp('diagnostics-test');
    dump = sl4f.Dump(dumpDir.path);
    diagnostics = sl4f.Diagnostics(sl4fDriver, dump);
  });

  tearDown(() async {
    dumpDir.deleteSync(recursive: true);

    await sl4fDriver.stopServer();
    sl4fDriver.close();
  });

  group(sl4f.Sl4f, () {
    test('dumpDiagnostics using diagnostics', () async {
      await diagnostics.dumpDiagnostics('test');
      expect(
          dumpDir.listSync().map((f) => f.path.split('/').last),
          unorderedMatches([
            matches(RegExp(r'-test-diagnostic-net-if.(txt|json)$')),
            matches(RegExp(r'-test-diagnostic-wlan.(txt|json)$')),
          ]));
    });
  }, timeout: Timeout(_timeout));
}
