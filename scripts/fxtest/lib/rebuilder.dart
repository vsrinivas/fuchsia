// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:io';

import 'package:fxtest/fxtest.dart';

Future<void> rebuildFuchsia() async {
  await Process.start('fx', ['build'], mode: ProcessStartMode.inheritStdio)
      .then((Process process) async {
    final _exitCode = await process.exitCode;
    if (_exitCode != 0) {
      throw BuildException('fx build', _exitCode);
    }
  });
}
