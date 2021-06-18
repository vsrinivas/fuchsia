// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:io';

bool isNullOrEmpty(String str) => str == null || str.isEmpty;

bool generatePublicKey(final String pkeyPath, final String pubkeyPath) {
  var keyGenProcess = Process.runSync('ssh-keygen', ['-y', '-f', pkeyPath]);
  if (keyGenProcess.exitCode != 0) {
    return false;
  }
  File(pubkeyPath).writeAsStringSync(keyGenProcess.stdout);
  return true;
}
