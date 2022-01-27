// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TODO(https://fxbug.dev/84961): Fix null safety and remove this language version.
// @dart=2.9

import 'dart:convert';
import 'dart:async';
import 'dart:io';

import 'package:path/path.dart' as path;

bool isNullOrEmpty(String str) => str == null || str.isEmpty;

Future<bool> generatePublicKey(
    final String pkeyPath, final String pubkeyPath) async {
  var keyGenProcess = Process.runSync('ssh-keygen', ['-y', '-f', pkeyPath]);
  if (keyGenProcess.exitCode != 0) {
    return false;
  }
  await File(pubkeyPath).writeAsString(keyGenProcess.stdout);
  return true;
}

/// An [FFXRunner] that creates an isolated environment for ffx.
// TODO(fxbug.dev/82518): Use isolated ffx when available.
class IsolatedFFXRunner {
  /// Temporary directory for ffx ascendd socket and logs
  final Directory _ffxDir =
      Directory.systemTemp.createTempSync('IsolatedFFXRunner');

  /// Environment variables for the ffx process.
  final Map<String, String> _env = {};

  String _configPath;

  String _ffxPath;

  IsolatedFFXRunner();

  Future<void> init(String ffxPath) async {
    _ffxPath = ffxPath;

    final socketPath = path.join(_ffxDir.path, 'ffx_socket');
    _configPath = path.join(_ffxDir.path, 'ffx_config.json');
    // TODO(fxbug.dev/64499): Stop setting environment variables once bug is fixed.
    _env['ASCENDD'] = socketPath;

    final config = {
      'overnet': {'socket': socketPath},
      'log': {'dir': path.join(_ffxDir.path, 'logs')},
      'fastboot': {
        'usb': {'disabled': true},
        'flash': {'min_timeout_secs': 60, 'timeout_rate': 5}
      },
      'ssh': {'priv': Platform.environment['FUCHSIA_SSH_KEY']}
    };

    await File(_configPath).writeAsString(json.encode(config));
  }

  Future<void> tearDown() async {
    await run(['daemon', 'stop']);
    await _ffxDir.delete(recursive: true);
  }

  /// Runs an ffx command to completion.
  /// Throws [FFXException] if ffx returns with a non-zero exit code.
  Future<String> run(List<String> args) async {
    final List<String> configArgs = ['--config', _configPath];
    final result = await Process.run(_ffxPath, configArgs + args,
        environment: _env, runInShell: true);
    final output = result.stdout.trim();
    final error = result.stderr.trim();

    if (exitCode != 0 || error.isNotEmpty) {
      throw Exception('Unexpected error running ffx:\n'
          'exit code $exitCode\n'
          'command $_ffxPath $args\n'
          'stdout: $output\n'
          'stderr: $error');
    }
    return output;
  }
}
