// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:io';

import 'package:test/test.dart';
import 'package:sl4f/sl4f.dart' as sl4f;

const _timeout = Duration(seconds: 60);

void main() {
  sl4f.Sl4f sl4fDriver;
  sl4f.Dump dump;
  Directory dumpDir;

  setUp(() async {
    sl4fDriver = sl4f.Sl4f.fromEnvironment();
    await sl4fDriver.startServer();

    dumpDir = await Directory.systemTemp.createTemp('dump-test');
    dump = sl4f.Dump(dumpDir.path);
  });

  tearDown(() async {
    dumpDir.deleteSync(recursive: true);

    await sl4fDriver.stopServer();
    sl4fDriver.close();
  });

  group(sl4f.Sl4f, () {
    test('dumpDiagnostics dumps diagnostics', () async {
      await sl4fDriver.dumpDiagnostics('test', dump: dump);

      expect(
          dumpDir.listSync().map((f) => f.path.split('/').last),
          unorderedMatches([
            matches(RegExp(r'-test-diagnostic-kstats.txt$')),
            matches(RegExp(r'-test-diagnostic-net-if.txt$')),
            matches(RegExp(r'-test-diagnostic-ps.txt$')),
            matches(RegExp(r'-test-diagnostic-top.txt$')),
            matches(RegExp(r'-test-diagnostic-wlan.txt$')),
          ]));
    });
  }, timeout: Timeout(_timeout));
}
