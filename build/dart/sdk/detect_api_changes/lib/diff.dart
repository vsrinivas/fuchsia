// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:io';

// Diff two API files. Return the results to the calling method.
String diffTwoFiles(String leftPath, String rightPath) {
  var leftAPIString = readFile(leftPath);
  var rightAPIString = readFile(rightPath);
  if (leftAPIString == rightAPIString) {
    return null;
  }

  // TODO(fxbug.dev/6541): Describe the differences.

  var message = 'Error: API has changed!\n'
      'Please acknowledge this change by running:\n'
      '    cp $leftPath $rightPath \n';
  return message;
}

String readFile(String path) {
  File f = File(path);
  if (!f.existsSync()) {
    throw FileSystemException('File $path does not exist');
  }
  return f.readAsStringSync();
}
