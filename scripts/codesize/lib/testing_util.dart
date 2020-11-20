// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:io';

/// Returns the run-time location of `//scripts/codesize/testdata`, regardless
/// of environment (dev, infra, etc.).
Directory locateTestData() {
  // See the `//scripts/codesize:bloaty_reports` target in `BUILD.gn`.
  final buildDirTestData =
      Directory('${Directory(Platform.environment['PWD']!).absolute.path}/'
          'host_x64/test_data/codesize');
  Directory testData;
  if (buildDirTestData.existsSync()) {
    // Running in `fx test` mode or infra.
    testData = buildDirTestData;
  } else {
    // Running from `pub run test`.
    final sourceDir = Platform.environment['FUCHSIA_DIR'];
    if (sourceDir == null)
      throw Exception('Missing the FUCHSIA_DIR environment variable.');
    testData = Directory(
        '${Directory(sourceDir).absolute.path}/scripts/codesize/testdata');
  }
  if (!testData.existsSync()) {
    throw Exception('Cannot find the $testData folder.');
  }
  return testData;
}
