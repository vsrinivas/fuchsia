// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:io';
import 'package:sl4f/sl4f.dart' as sl4f;
import 'package:test/test.dart';

void main() {
  final sl4fDriver = sl4f.Sl4f.fromEnvironment();
  sl4f.Device deviceController;

  setUp(() async {
    await sl4fDriver.startServer();
    deviceController = sl4f.Device(sl4fDriver);
  });

  tearDown(() async {
    await sl4fDriver.stopServer();
    sl4fDriver.close();
  });

  test('test flashing', () async {
    // Get env info necessary to run test.
    String fbSernum = Platform.environment['FUCHSIA_FASTBOOT_SERNUM'];
    if (fbSernum.isEmpty) {
      fail('FUCHSIA_FASTBOOT_SERNUM environment variable was empty.');
    }
    String nodename = Platform.environment['FUCHSIA_NODENAME'];
    if (nodename.isEmpty) {
      fail('FUCHSIA_NODENAME environment variable was empty.');
    }

    // Get the device into fastboot.
    await sl4fDriver.ssh.run('dm reboot-bootloader');

    // Wait 30 seconds for the device to enter fastboot.
    sleep(Duration(seconds: 30));

    // Run flash.sh.
    var process = Process.runSync('./flash.sh', ['-s', fbSernum]);
    stdout.write(process.stdout);
    stderr.write(process.stderr);
    expect(process.exitCode, equals(0),
        reason: 'flash.sh exited with nonzero exit code');

    // Verify that the device came back by checking the nodename.
    expect(deviceController.getDeviceName, equals(nodename),
        reason: 'could not get nodename via sl4f');
  }, timeout: Timeout(Duration(minutes: 2)));
}
