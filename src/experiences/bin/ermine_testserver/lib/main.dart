// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TODO(https://fxbug.dev/84961): Fix null safety and remove this language version.
// @dart=2.9

import 'dart:io';

import 'package:ermine_localhost/localhost.dart';
import 'package:fuchsia_logger/logger.dart';

import 'src/file_group.dart';

/// See go/workstation_localhost for more details about using this component.
// TODO(fxb/68320): Add UIs that can change the test case.
void main(List<String> args) async {
  setupLogger(name: 'ermine_testserver');

  final localhost = Localhost();
  final url = await localhost.bindServer();
  log.info('Server is ready: $url');

  final fileGroup = fileGroups[TestCase.simpleBrowserTest];
  for (var fileName in fileGroup.fileList) {
    final index = fileGroup.fileList.indexOf(fileName);
    final path = fileGroup.getPath(index);
    final file = File(path);
    if (file.existsSync()) {
      localhost.passWebFile(file);
    } else {
      log.warning('No such file or directory: $path');
    }
  }

  localhost.startServing();
}
