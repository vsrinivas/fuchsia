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
  sl4f.Performance performance;

  setUp(() async {
    sl4fDriver = sl4f.Sl4f.fromEnvironment();
    await sl4fDriver.startServer();
    dumpDir = await Directory.systemTemp.createTemp('temp-dump');
    dump = sl4f.Dump(dumpDir.path);

    performance = sl4f.Performance(sl4fDriver, dump);
  });

  tearDown(() async {
    dumpDir.deleteSync(recursive: true);

    await sl4fDriver.stopServer();
    sl4fDriver.close();
  });

  group(sl4f.Sl4f, () {
    test('trace and download', () async {
      expect(
          await performance.trace(
              duration: Duration(seconds: 2), traceName: 'test-trace'),
          equals(true));

      await performance.downloadTraceFile('test-trace');

      expect(
          dumpDir.listSync().map((f) => f.path.split('/').last),
          unorderedMatches([
            matches(RegExp(r'-test-trace-trace.json$')),
          ]));
    });
  }, timeout: Timeout(_timeout));
}
