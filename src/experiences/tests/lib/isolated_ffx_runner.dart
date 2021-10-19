// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:async';
import 'dart:convert';
import 'dart:io';

import 'package:ffx/ffx.dart';
import 'package:path/path.dart' as path;

/// An [FfxRunner] that creates an isolated environment for ffx.
// TODO(fxbug.dev/82518): Run ffx in isolation with built in means
class IsolatedFfxRunner extends FfxRunner {
  /// Temporary directory for ffx ascendd socket and logs
  final Directory _ffxDir =
      Directory.systemTemp.createTempSync('IsolatedFfxRunner');

  /// Environment variables for the ffx process.
  final Map<String, String> _env = {};

  /// Extra arguments passed to ffx.
  late List<String> _extraArgs;

  /// The process for the `ffx daemon start` command.
  Process? _ffxDaemon;

  IsolatedFfxRunner(String ffxPath) : super(ffxPath) {
    final socketPath = path.join(_ffxDir.path, 'ascendd');
    final config = {
      'overnet.socket': socketPath,
      'log.dir': path.join(_ffxDir.path, 'logs')
    };
    _extraArgs = config.entries
        .expand((entry) => ['--config', '${entry.key}=${entry.value}'])
        .toList();

    // TODO(fxbug.dev/64499): ffx does not use overnet.socket config in Hoist
    _env['ASCENDD'] = socketPath;
  }

  Future<void> setUp() async {
    // Start the ffx daemon in the current working directory.
    // This ensures that ffx can read the SSH private key from $FUCHSIA_SSH_KEY
    // TODO(fxbug.dev/85089): Remove this workaround
    _ffxDaemon = await start(['daemon', 'start']);
  }

  Future<void> tearDown() async {
    try {
      await run(['daemon', 'stop']);
    } on FfxException catch (e) {
      print('Warning: `ffx daemon stop` did not exit cleanly:\n$e');
    } finally {
      _ffxDaemon!.kill();
    }
    await _ffxDir.delete(recursive: true);
  }

  /// Runs an ffx command to completion.
  ///
  /// Throws [FfxException] if ffx returns with a non-zero exit code.
  @override
  Future<Process> run(List<String> args) async {
    final process = await start(args);
    final exitCode = await process.exitCode;
    if (exitCode != 0) {
      final stdout = await process.stdout.transform(utf8.decoder).join('\n');
      final stderr = await process.stderr.transform(utf8.decoder).join('\n');
      throw FfxException('Unexpected error running ffx:\n'
          'exit code $exitCode\n'
          'command $ffxPath $args\n'
          'stdout: $stdout\n'
          'stderr: $stderr');
    }
    return process;
  }

  /// Starts an ffx command but does not wait for completion.
  Future<Process> start(List<String> args) async {
    return Process.start(ffxPath, _extraArgs + args,
        environment: _env, runInShell: true, mode: ProcessStartMode.normal);
  }
}
