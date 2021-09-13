// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
import 'dart:convert';
import 'dart:io';

import 'exceptions.dart';

/// A helper that runs the ffx binary and returns its output.
class FfxRunner {
  /// Path to the ffx executable.
  final String ffxPath;

  FfxRunner([this.ffxPath = 'ffx']);

  Future<Process> run(List<String> args) async {
    final process = await Process.start(ffxPath, args,
        runInShell: true, mode: ProcessStartMode.normal);
    final exitCode = await process.exitCode;
    if (exitCode != 0) {
      throw FfxException('Unexpected error running ffx:\n'
          'exit code $exitCode\n'
          'command: ffx $args\n');
    }
    return process;
  }

  Future<Stream<String>> runWithOutput(List<String> args) async {
    final process = await run(args);
    return process.stdout.transform(utf8.decoder);
  }
}
