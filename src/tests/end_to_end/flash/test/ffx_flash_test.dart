// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:convert';
import 'dart:io';
import 'package:logging/logging.dart';
import 'package:retry/retry.dart';
import 'package:src.tests.end_to_end.flash._ffx_flash_test_dart_library/utils.dart'
    as utils;
import 'package:test/test.dart';

class DeviceUndiscoverableException implements Exception {
  final String error;

  DeviceUndiscoverableException(this.error);

  @override
  String toString() => error;
}

void main() {
  final log = Logger('ffx_flash_test');
  final outputDir = Directory.current;
  final runtimeDepsPath = Platform.script.resolve('runtime_deps').toFilePath();
  final ffxPath = Platform.script.resolve('$runtimeDepsPath/ffx').toFilePath();
  final flashJSONPath =
      Platform.script.resolve('${outputDir.path}/flash.json').toFilePath();

  setUpAll(() async {
    Logger.root
      ..level = Level.ALL
      ..onRecord.listen((rec) => print('[${rec.level}]: ${rec.message}'));
  });

  Future<List> getTarget(final String nodename) async {
    final ffxTargetListArgs = ['target', 'list', '-f', 'j'];

    log.info('Listing target with ffx $ffxTargetListArgs');

    final result = await Process.run(ffxPath, ffxTargetListArgs);
    final output = result.stdout.trim();
    final error = result.stderr;

    log.info('Output from ffx target list: $output');

    expect(result.exitCode, equals(0),
        reason: 'ffx target list exited with nonzero exit code: $error');

    if (output.isEmpty) {
      return [];
    }

    // Filter the discoverable targets to just the specific nodename.
    final jsonOutput = jsonDecode(output) as List;

    return jsonOutput.where((val) => val['nodename'] == nodename).toList();
  }

  Future<List> waitForDeviceToComeUp(final String nodename,
      {int attempts = 6, Duration delay = const Duration(seconds: 10)}) async {
    return await retry(() async {
      var targetStatePostFlash = await getTarget(nodename);

      if (targetStatePostFlash.length == 1) {
        return targetStatePostFlash;
      }
      throw DeviceUndiscoverableException(
          'device $nodename is undiscoverable.');
    },
        retryIf: (e) => e is DeviceUndiscoverableException,
        maxAttempts: attempts,
        delayFactor: delay,
        maxDelay: delay,
        randomizationFactor: 0.0,
        onRetry: (e) => log.info('Retry due to error: ${e.toString()}'));
  }

  test('test ffx target flash', () async {
    String nodename = Platform.environment['FUCHSIA_NODENAME'];

    if (utils.isNullOrEmpty(nodename)) {
      fail('FUCHSIA_NODENAME environment variable was empty.');
    }

    String pkey = Platform.environment['FUCHSIA_SSH_KEY'];
    if (utils.isNullOrEmpty(pkey)) {
      fail('FUCHSIA_SSH_KEY environment variable was empty');
    }

    final publicKeyPath = '${outputDir.path}/pubkey';
    // Generate a public key from the private key.
    if (!utils.generatePublicKey(pkey, publicKeyPath)) {
      fail('ssh-keygen failed to generate public key from private key');
    }

    final ffxDoctorRestartDaemonArgs = ['doctor', '--restart-daemon'];

    log.info('Restarting daemon with ffx $ffxDoctorRestartDaemonArgs');

    // Force restart the daemon to make sure no old daemon is used.
    var result = await Process.run(ffxPath, ffxDoctorRestartDaemonArgs);
    var error = result.stderr;
    var output = result.stdout;

    expect(result.exitCode, equals(0),
        reason: 'ffx doctor was not able to restart daemon: $error');

    final targetStatePreFlash = await getTarget(nodename);

    expect(targetStatePreFlash.length, equals(1),
        reason: 'ffx target list did not find the target');

    // Make sure target state is in product state pre flashing.
    final statePreFlash = targetStatePreFlash[0]['target_state'];
    expect(statePreFlash, equals('Product'),
        reason: '$nodename is in $statePreFlash, want Product state');

    final ffxTargetFlashArgs = [
      '--target',
      nodename,
      'target',
      'flash',
      '--ssh-key',
      publicKeyPath,
      flashJSONPath,
      'fuchsia'
    ];

    // Run ffx target flash.
    log.info('Flashing target with ffx $ffxTargetFlashArgs');

    result = await Process.run(ffxPath, ffxTargetFlashArgs);
    error = result.stderr;
    output = result.stdout;

    log.info('Output from ffx target flash: $output');

    expect(result.exitCode, equals(0),
        reason: 'ffx target flash exited with nonzero exit code: $error');

    final targetStatePostFlash = await waitForDeviceToComeUp(nodename);

    expect(targetStatePostFlash.length, equals(1),
        reason: 'ffx target list did not find the target');

    // Make sure target state is in product state post flashing.
    final statePostFlash = targetStatePostFlash[0]['target_state'];
    expect(statePostFlash, equals('Product'),
        reason: '$nodename is in $statePostFlash, want Product state');
  }, timeout: Timeout(Duration(minutes: 15)));
}
