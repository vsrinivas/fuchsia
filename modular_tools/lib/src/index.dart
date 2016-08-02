// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:async';

import 'package:path/path.dart' as path;

import 'base/process.dart';
import 'configuration.dart';

class IndexGenerator {
  final EnvironmentConfiguration _environment;

  const IndexGenerator(this._environment);

  Future<int> generateIndex() {
    final String dartBinary =
        path.join(_environment.dartSdkPath, 'bin', 'dart');
    final String indexerTool = path.join(
        _environment.modularRoot, 'indexer', 'pipeline', 'bin', 'run.dart');
    final List<String> args = <String>[
      indexerTool,
      '--host=https://tq.mojoapps.io/',
      '--host-root=${_environment.modularRoot}',
      '--output-directory=${_environment.buildDir}',
      _environment.base,
    ];
    return processNonBlocking(dartBinary, args);
  }
}
