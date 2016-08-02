// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:async';

import 'package:path/path.dart' as path;
import 'package:yaml/yaml.dart';

import 'base/process.dart';
import 'configuration.dart';
import 'flutter_builder.dart';
import 'mojo_builder.dart';

class BuildRunner {
  final EnvironmentConfiguration _environment;
  final bool _release;

  BuildRunner(this._environment, this._release);

  Future<int> runBuild() {
    return parallelStream(_environment.projectConfigurations, _buildSnapshot);
  }

  Future<int> _buildSnapshot(ProjectConfiguration project) async {
    if (project.targets == null) return 0;
    for (final target in project.targets) {
      final ret = await _buildTarget(target, project);
      if (ret != 0) return ret;
    }
    return 0;
  }

  Future<int> _buildTarget(YamlMap target, ProjectConfiguration project) {
    if (!target.containsKey('output_name') ||
        !target.containsKey('entry_point')) {
      print("Either `output_name` or `entry_point` missing from target.");
      return new Future.value(2);
    }

    final main = path.join(project.projectRoot, target['entry_point']);
    final outputFile = path.join(_environment.buildDir, target['output_name']);
    final depsFile = outputFile + '.d';
    final snapshotPath = outputFile + '.bin';
    final List<String> otherDeps = target.containsKey('assets')
        ? [path.join(project.projectRoot, target['assets'])]
        : [];
    final List<String> deployedAssets = project.deployedAssets
            ?.map((final String asset) => path.join(project.projectRoot, asset))
            ?.toList() ??
        [];
    final builder = (project.projectType == ProjectType.dart)
        ? new MojoBuilder(_environment, project)
        : new FlutterBuilder(_environment, project,
            assetManifestPath: target['assets']);
    return builder.rebuildIfRequired(main, outputFile, depsFile, snapshotPath,
        deployedAssets, otherDeps, _release);
  }
}
