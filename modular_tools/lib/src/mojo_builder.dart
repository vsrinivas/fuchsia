// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TODO(alhaad): This is a hack to build pure-dart mojo files until the mojo
// team supports a tool to do this. Or even better, move this into the mojo
// repo?

import 'dart:async';
import 'dart:io';
import 'dart:typed_data';

import 'package:archive/archive.dart';
import 'package:path/path.dart' as path;

import 'base/file_system.dart';
import 'base/logging.dart';
import 'base/process.dart';
import 'builder.dart';
import 'configuration.dart';

// TODO(alhaad): Running a binary in release mode aka checked mode should be
// a run time configuration.
const String _debugShebang = '#!mojo mojo:dart_content_handler?strict=true\n';
const String _releaseShebang = '#!mojo mojo:dart_content_handler\n';

class MojoBuilder extends AppBuilder {
  String _dartSnapshotter;

  MojoBuilder(
      EnvironmentConfiguration environment, ProjectConfiguration project)
      : _dartSnapshotter = path.join(
            environment.modularRoot,
            'dart-packages',
            'modular',
            'lib',
            'cache',
            environment.mojoRevision,
            hostPlatformToArchString[environment.hostPlatform],
            'dart_snapshotter'),
        super(environment, project) {
    ;
    File snapshotterFile = new File(_dartSnapshotter);
    if (!snapshotterFile.existsSync()) {
      throw new Exception("Dart snapshotter ($_dartSnapshotter) missing.");
    }
  }

  Future<int> build(String mainPath, String outputPath, String depsFilePath,
      String snapshotPath, bool isRelease) async {
    final archive = new Archive();
    await ensureDirectoryExists(snapshotPath);
    int compileResult =
        await _compile(mainPath, snapshotPath, outputPath, depsFilePath);
    if (compileResult != 0) {
      logging.severe(
          'Failed to run the dart_snapshotter. Exit code: $compileResult');
      return compileResult;
    }

    archive.addFile(await _createSnapshotFile(snapshotPath));
    Uint8List zipBytes =
        new Uint8List.fromList(new ZipEncoder().encode(archive));
    await ensureDirectoryExists(outputPath);
    RandomAccessFile outputFile =
        await (new File(outputPath).open(mode: FileMode.WRITE));

    await outputFile.writeString(isRelease ? _releaseShebang : _debugShebang);
    await outputFile.writeFrom(zipBytes);
    await outputFile.close();
    print('Built $outputPath.');
    return 0;
  }

  Future<ArchiveFile> _createSnapshotFile(String snapshotPath) async {
    File file = new File(snapshotPath);
    List<int> content = await file.readAsBytes();
    return new ArchiveFile('snapshot_blob.bin', content.length, content);
  }

  Future<int> _compile(String mainPath, String snapshotPath, String outputPath,
      String depsFilePath) {
    return processNonBlocking(_dartSnapshotter, [
      mainPath,
      '--package-root=${path.absolute(project.packageRoot)}',
      '--snapshot=${path.absolute(snapshotPath)}',
      '--depfile=${path.absolute(depsFilePath)}',
      '--build-output=${path.absolute(outputPath)}'
    ]);
  }
}
