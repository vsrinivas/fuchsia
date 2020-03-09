// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:async';
import 'dart:core' show RegExp;
import 'dart:io' show Platform, Process;

import 'package:test/test.dart';

// In order to run these tests, the host should be connected to exactly one
// Zedmon device, satisfying:
//  - Hardware version 2.1 (version is printed on the board);
//  - Firmware built from the Zedmon repository's revision 9765b27b5f, or
//    equivalent.
Future<void> main() async {
  final _zedmonPath =
      Platform.script.resolve('runtime_deps/zedmon').toFilePath();

  // "zedmon list" should yield exactly one word, containing a serial number.
  test('zedmon list', () async {
    final result = await Process.run(_zedmonPath, ['list']);
    expect(result.exitCode, 0);

    final regex = RegExp(r'\W+');
    expect(regex.allMatches(result.stdout).length, 1);
  });
}
