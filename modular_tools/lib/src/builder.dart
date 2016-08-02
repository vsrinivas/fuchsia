// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:async';
import 'dart:io';

import 'package:path/path.dart' as path;

import 'configuration.dart';
import 'dependencies.dart';

abstract class AppBuilder {
  final EnvironmentConfiguration environment;
  final ProjectConfiguration project;

  AppBuilder(this.environment, this.project);

  Future<int> build(String mainPath, String outputPath, String depsFilePath,
      String snapshotPath, bool isRelease);

  String _getProjectOutputName(final String outputPath) {
    assert(outputPath != null);

    // Test that the output has the correct extension, for sanity.
    String extension = path.extension(outputPath);
    assert(extension == '.flx' || extension == '.mojo');

    String name = path.basenameWithoutExtension(outputPath);
    assert(name != null || name.isNotEmpty);

    return name;
  }

  Future<bool> _buildDeployedAssets(
      final String outputPath, final List<String> deployedAssets) async {
    assert(deployedAssets != null);

    String projectName = _getProjectOutputName(outputPath);

    // Get the output directory path.
    for (final String asset in deployedAssets) {
      final File src = new File(path.join(project.projectRoot, asset));
      if (!await src.exists()) {
        print('Asset file does not exist: $src');
        return false;
      }

      // We place all assets under a directory that has the same name as the
      // project output.
      String dstDirPath = path.join(environment.buildDir, projectName);
      await new Directory(dstDirPath).create();

      String dstAssetPath = path.join(dstDirPath, path.basename(asset));
      await src.copy(dstAssetPath);
    }
    return true;
  }

  Future<int> rebuildIfRequired(
      String mainPath,
      String outputPath,
      String depsFilePath,
      String snapshotPath,
      List<String> deployedAssets,
      List<String> otherDeps,
      bool isRelease) async {
    final depsFile = new File(depsFilePath);
    final List<File> extraDepFiles = ([]
          ..addAll(deployedAssets)
          ..addAll(otherDeps))
        .map((final String path) => new File(path))
        .toList();
    if (!(await requiresRebuild(
        new File(outputPath),
        await parseDependenciesFile(depsFile, extraDepFiles),
        new File(path.join(project.projectRoot, '.packages'))))) {
      return 0;
    }

    int result = await build(
        mainPath, outputPath, depsFilePath, snapshotPath, isRelease);
    if (result != 0) {
      if (await depsFile.exists()) {
        await depsFile.delete();
      }
    }

    if (deployedAssets.isNotEmpty) {
      if (!await _buildDeployedAssets(outputPath, deployedAssets)) {
        if (await depsFile.exists()) {
          await depsFile.delete();
        }
        return 1;
      }
    }

    return result;
  }
}
