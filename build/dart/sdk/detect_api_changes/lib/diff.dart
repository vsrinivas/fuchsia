// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TODO(https://fxbug.dev/84961): Fix null safety and remove this language version.
// @dart=2.9

import 'dart:io';
import 'package:path/path.dart' as p;

// Diff two API files. Return the results to the calling method.
String diffTwoFiles(String leftPath, String rightPath, String fuchsiaRoot) {
  var leftAPIString = readFile(leftPath);
  var rightAPIString = readFile(rightPath);
  if (leftAPIString == rightAPIString) {
    return '';
  }

  var leftPathRel = p.relative(leftPath, from: fuchsiaRoot);
  var rightPathRel = p.relative(rightPath, from: fuchsiaRoot);

  // TODO(fxbug.dev/6541): Describe the differences.

  var message = 'Error: API has changed!\n'
      'Please acknowledge this change by running:\n'
      '    cp $leftPathRel $rightPathRel';
  return message;
}

String readFile(String path) {
  File f = File(path);
  if (!f.existsSync()) {
    throw FileSystemException('File $path does not exist');
  }
  return f.readAsStringSync();
}
