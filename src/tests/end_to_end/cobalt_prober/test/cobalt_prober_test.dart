// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:test/test.dart';
import 'package:sl4f/sl4f.dart' as sl4f;

const _timeout = Timeout(Duration(minutes: 5));

void main(List<String> arguments) {
  sl4f.Sl4f sl4fDriver;

  setUp(() async {
    sl4fDriver = sl4f.Sl4f.fromEnvironment();
    await sl4fDriver.startServer();
  });

  tearDown(() async {
    await sl4fDriver.stopServer();
    sl4fDriver.close();
  });

  group('cobalt_prober', () {
    test('Run prober', () async {
      await sl4f.DeviceLog(sl4fDriver)
          .info('Starting cobalt testapp for prober');
      print('Running cobalt testapp for prober on the device');
      final result = await sl4fDriver.ssh.run(
          'run fuchsia-pkg://fuchsia.com/cobalt-prober-do-not-run-manually#meta/cobalt_testapp_for_prober_do_not_run_manually.cmx');
      expect(result.exitCode, equals(0));
    });
  }, timeout: _timeout);
}
