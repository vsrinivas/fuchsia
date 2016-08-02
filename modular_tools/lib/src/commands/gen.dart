// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:async';

import 'package:path/path.dart' as path;

import '../base/process.dart';
import 'modular_command.dart';

class GenCommand extends ModularCommand {
  final String name = 'gen';
  final String description = 'Generate dart bindings.';

  // |dirsToSkip| are paths relative to |modularRoot|.
  Future<int> _generateDartBindings(
      String packagesDir, List<String> dirsToSkip) async {
    final String pubBinary = path.join(environment.dartSdkPath, 'bin', 'pub');
    final String mojoSdk = path.join(environment.modularRoot, 'third_party',
        'mojo', 'src', 'mojo', 'public');

    return await process(
        pubBinary,
        [
          'run',
          'mojom',
          'gen',
          '--force',
          '-m',
          mojoSdk,
          '-r',
          packagesDir,
          '-s',
          dirsToSkip.map((dir) {
            return path.join(environment.modularRoot, dir);
          }).join(','),
          '--output',
          packagesDir
        ],
        workingDirectory: path.join(environment.modularRoot, 'modular_tools'));
  }

  @override
  Future<int> runInProject() async {
    final int rootLevelPackages = await _generateDartBindings(
        environment.modularRoot, ['third_party', 'out', 'public']);
    if (rootLevelPackages != 0) {
      return rootLevelPackages;
    }

    final publicPackage = await _generateDartBindings(
        path.join(environment.modularRoot, 'dart-packages'), ['third_party', 'out']);
    if (publicPackage != 0) {
      return publicPackage;
    }

    return 0;
  }
}
