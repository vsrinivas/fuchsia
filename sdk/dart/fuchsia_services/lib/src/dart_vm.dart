// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:io';

// A utility file for discovering and connecting to the Dart VM.

// return Dart VM service port which can be used to connect to for tests and
// devtools.
String? getVmServicePort() {
  List<FileSystemEntity> files;
  try {
    files = Directory('/tmp/dart.services').listSync();
  } on FileSystemException {
    return null;
  }
  return files.isEmpty ? null : files.first.path.split('/').last;
}
