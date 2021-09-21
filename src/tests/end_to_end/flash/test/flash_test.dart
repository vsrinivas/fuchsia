// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TODO(https://fxbug.dev/84961): Fix null safety and remove this language version.
// @dart=2.9

import 'dart:io';
import 'package:sl4f/sl4f.dart' as sl4f;
import 'package:test/test.dart';

bool _isNullOrEmpty(String str) => str == null || str.isEmpty;

void main() {
  test('test flashing', () async {
    // Get env info necessary to run test.
    String fbSernum = Platform.environment['FUCHSIA_FASTBOOT_SERNUM'];
    if (_isNullOrEmpty(fbSernum)) {
      fail('FUCHSIA_FASTBOOT_SERNUM environment variable was empty.');
    }
    String nodename = Platform.environment['FUCHSIA_NODENAME'];
    if (_isNullOrEmpty(nodename)) {
      fail('FUCHSIA_NODENAME environment variable was empty.');
    }
    String ipv4 = Platform.environment['FUCHSIA_DEVICE_ADDR'];
    if (_isNullOrEmpty(ipv4)) {
      fail('FUCHSIA_DEVICE_ADDR environment variable was empty');
    }
    String pkey = Platform.environment['FUCHSIA_SSH_KEY'];
    if (_isNullOrEmpty(pkey)) {
      fail('FUCHSIA_SSH_KEY environment variable was empty');
    }

    // Generate a public key from the private key.
    if (!await generatePublicKey(pkey, './pubkey')) {
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

Future<bool> generatePublicKey(
    final String pkeyPath, final String pubkeyPath) async {
  var keyGenProcess = Process.runSync('ssh-keygen', ['-y', '-f', pkeyPath]);
  if (keyGenProcess.exitCode != 0) {
    return false;
  }
  await File(pubkeyPath).writeAsString(keyGenProcess.stdout);
  return true;
}
