// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:io';
import 'package:sl4f/sl4f.dart' as sl4f;
import 'package:src.tests.end_to_end.flash._flash_script_test_dart_library/utils.dart'
    as utils;
import 'package:test/test.dart';

void main() {
  test('test flashing', () async {
    // Get env info necessary to run test.
    String fbSernum = Platform.environment['FUCHSIA_FASTBOOT_SERNUM'];
    if (utils.isNullOrEmpty(fbSernum)) {
      fail('FUCHSIA_FASTBOOT_SERNUM environment variable was empty.');
    }
    String nodename = Platform.environment['FUCHSIA_NODENAME'];
    if (utils.isNullOrEmpty(nodename)) {
      fail('FUCHSIA_NODENAME environment variable was empty.');
    }
    String ipv4 = Platform.environment['FUCHSIA_DEVICE_ADDR'];
    if (utils.isNullOrEmpty(ipv4)) {
      fail('FUCHSIA_DEVICE_ADDR environment variable was empty');
    }
    String pkey = Platform.environment['FUCHSIA_SSH_KEY'];
    if (utils.isNullOrEmpty(pkey)) {
      fail('FUCHSIA_SSH_KEY environment variable was empty');
    }

    // Generate a public key from the private key.
    if (!utils.generatePublicKey(pkey, './pubkey')) {
      fail('ssh-keygen failed to generate public key from private key');
    }

    // Get the device into fastboot.
    sl4f.Sl4f client = sl4f.Sl4f.fromEnvironment();
    await client.ssh.run('dm reboot-bootloader');

    // Wait 30 seconds for the device to enter fastboot.
    sleep(Duration(seconds: 30));

    // Run flash.sh.
    var process =
        Process.runSync('./flash.sh', ['--ssh-key=./pubkey', '-s', fbSernum]);
    stdout.write(process.stdout);
    stderr.write(process.stderr);
    expect(process.exitCode, equals(0),
        reason: 'flash.sh exited with nonzero exit code');

    // Wait 45 seconds for the device to enter fuchsia after flashing.
    sleep(Duration(seconds: 45));

    // Verify that the device came back by checking the nodename over sl4f.
    client = sl4f.Sl4f.fromEnvironment();
    await client.startServer();
    sl4f.Device deviceController = sl4f.Device(client);
    expect(await deviceController.getDeviceName(), equals(nodename),
        reason: 'could not get nodename via sl4f');
    await client.stopServer();
    client.close();
  }, timeout: Timeout(Duration(minutes: 5)));
}
