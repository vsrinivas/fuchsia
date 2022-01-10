// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TODO(https://fxbug.dev/84961): Fix null safety and remove this language version.
// @dart=2.9

import 'dart:async';
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
  utils.IsolatedFFXRunner _ffxRunner;
  StreamSubscription _sigintSub;

  Future<void> closeSigIntListener() async {
    if (_sigintSub != null) {
      await _sigintSub.cancel();
    }
  }

  void cleanUpAndExit(ProcessSignal signal, [int processExitCode = 2]) async {
    if (_ffxRunner != null) {
      await _ffxRunner.tearDown();
    }
    exitCode = processExitCode;
    await closeSigIntListener();
    exit(exitCode);
  }

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

    // Create a ffx client.
    _ffxRunner = utils.IsolatedFFXRunner();
    await _ffxRunner.init(ffxPath);

    // Ensure ffx gets stopped even if ctrl+c is called.
    _sigintSub = ProcessSignal.sigint.watch().listen(cleanUpAndExit);
  });

  tearDownAll(() async {
    await _ffxRunner.tearDown();
    await closeSigIntListener();
  });

  Future<Map<String, dynamic>> getTarget(final String nodename) async {
    final ffxTargetListArgs = ['target', 'list', '-f', 'j'];

    log.info('Listing target with ffx $ffxTargetListArgs');

    final output = await _ffxRunner.run(ffxTargetListArgs);

    log.info('Output from ffx target list: $output');

    if (output.isEmpty) {
      return {};
    }

    // Filter the discoverable targets to just the specific nodename.
    final jsonOutput = jsonDecode(output) as List;

    final filteredOutput =
        jsonOutput.where((val) => val['nodename'] == nodename).toList();
    return filteredOutput.isEmpty ? {} : filteredOutput[0];
  }

  Future<Map<String, dynamic>> waitForDeviceToComeUp(final String nodename,
      {int attempts = 6, Duration delay = const Duration(seconds: 15)}) async {
    return await retry(() async {
      var targetStatePostFlash = await getTarget(nodename);

      if (targetStatePostFlash.isNotEmpty) {
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
    if (!await utils.generatePublicKey(pkey, publicKeyPath)) {
      fail('ssh-keygen failed to generate public key from private key');
    }

    final targetStatePreFlash = await waitForDeviceToComeUp(nodename);

    expect(targetStatePreFlash.isNotEmpty, isTrue,
        reason: 'ffx target list did not find the target');

    // Make sure target state is in product state pre flashing.
    final statePreFlash = targetStatePreFlash['target_state'];
    expect(statePreFlash, equals('Product'),
        reason: '$nodename is in $statePreFlash, want Product state');

    final ffxTargetFlashArgs = [
      '--target',
      nodename,
      'target',
      'flash',
      '--ssh-key',
      publicKeyPath,
      flashJSONPath
    ];

    // Run ffx target flash.
    log.info('Flashing target with ffx $ffxTargetFlashArgs');

    final output = await _ffxRunner.run(ffxTargetFlashArgs);

    log.info('Output from ffx target flash: $output');

    final targetStatePostFlash = await waitForDeviceToComeUp(nodename);

    expect(targetStatePostFlash.isNotEmpty, isTrue,
        reason: 'ffx target list did not find the target');

    // Make sure target state is in product state post flashing.
    final statePostFlash = targetStatePostFlash['target_state'];
    expect(statePostFlash, equals('Product'),
        reason: '$nodename is in $statePostFlash, want Product state');
  }, timeout: Timeout(Duration(minutes: 15)));
}
