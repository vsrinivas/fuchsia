// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:async';

import 'package:path/path.dart' as path;

import 'base/process.dart';
import 'builder.dart';
import 'configuration.dart';
import 'dart:io';

const String _shebang = '#!mojo mojo:flutter\n';

class FlutterBuilder extends AppBuilder {
  // Path to a YAML file with the manifest of all assets used in this app.
  final String assetManifestPath;

  FlutterBuilder(
      EnvironmentConfiguration environment, ProjectConfiguration project,
      {this.assetManifestPath})
      : super(environment, project);

  Future<int> build(String mainPath, String outputPath, String depsFilePath,
      String snapshotPath, bool isRelease) async {
    final String tempPath = "$outputPath.tmp";
    final List<String> flutterArgs = [
      'build',
      'flx',
      '--target',
      mainPath,
      '--output-file',
      tempPath,
      '--snapshot',
      snapshotPath,
      '--depfile',
      depsFilePath,
      '--no-pub',
      // Significantly reduces flx size with the assumption that it only runs
      // on platforms with roboto fonts (like android).
      '--no-include-roboto-fonts',
    ];

    if (assetManifestPath != null) {
      flutterArgs.add('--manifest');
      flutterArgs.add(assetManifestPath);
    }

    final int result = await processNonBlocking(
        path.join(environment.flutterRoot, 'bin', 'flutter'), flutterArgs,
        workingDirectory: project.projectRoot);

    if (result == 0) {
      final IOSink sink = new File(outputPath).openWrite();
      sink.write(_shebang);
      final File tempFile = new File(tempPath);
      await sink.addStream(await tempFile.openRead());
      await sink.close();
      await tempFile.delete();
    }

    return new Future.value(result);
  }
}
