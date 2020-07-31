// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// @dart = 2.8

/// Functions for running bloaty over some ELF binaries.
library bloaty;

import 'dart:collection';
import 'dart:core';
import 'dart:io';

import 'package:pool/pool.dart';

import 'build.dart';
import 'io.dart';

class RunBloatyOptions {
  final Build build;

  final HashMap<String, SplayTreeSet<BuildArtifact>> artifactsByBuildId;
  final HashMap<String, SplayTreeSet<File>> debugBinaries;
  final HashMap<String, File> buildIdToLinkMapFile;

  final void Function(int) jobInitCallback;
  final void Function() jobIterationCallback;
  final void Function() jobCompleteCallback;

  RunBloatyOptions(
      {this.build,
      this.artifactsByBuildId,
      this.debugBinaries,
      this.buildIdToLinkMapFile,
      this.jobInitCallback,
      this.jobIterationCallback,
      this.jobCompleteCallback});

  /// Get the CIPD-compatible name of the host operating system.
  /// This is used to locate the bloaty prebuilt.
  String get hostPlatform {
    if (Platform.isLinux) return 'linux-x64';
    if (Platform.isMacOS) return 'mac-x64';
    throw Exception('Unsupported operating system');
  }
}

/// Runs bloaty on all binaries in the set `matchedDebugBinaries`, saving their
/// results in report files next to the binaries themselves.
/// `matchedDebugBinaries` should be a set of build-ids.
///
/// If `options.buildIdToLinkMapFile` contains a link map for the corresponding
/// binary, also supply that option to bloaty. Link maps will help bloaty
/// produce much more accurate size breakdowns.
Future<void> runBloatyOnMatchedBinaries(Set<String> matchedDebugBinaries,
    {RunBloatyOptions options}) async {
  final io = Io.get();
  final fuchsiaDir = Directory(Platform.environment['FUCHSIA_DIR']);
  final pathToBloaty = fuchsiaDir /
      Directory('prebuilt/third_party/bloaty/${options.hostPlatform}/bloaty');
  final pool = Pool(Platform.numberOfProcessors);
  final allFutures = <Future>[];
  options.jobInitCallback?.call(matchedDebugBinaries.length);
  for (final buildId in matchedDebugBinaries) {
    final blob = options.artifactsByBuildId[buildId].first;
    final debugElf = options.debugBinaries[buildId].first;
    List<String> linkMapArgs = [];
    if (options.buildIdToLinkMapFile.containsKey(buildId)) {
      linkMapArgs = [
        '--link-map-file=${options.buildIdToLinkMapFile[buildId].absolute.path}'
      ];
    }
    allFutures.add(pool.withResource(() async {
      final result = await io.processManager.run([
        pathToBloaty.absolute.path,
        '--pb',
        '-d',
        'compileunits,symbols',
        '-n',
        '0',
        '-s',
        'file',
        '--demangle=full',
        '--debug-file=${debugElf.absolute.path}',
        ...linkMapArgs,
        options.build.openFile(blob.buildPath).absolute.path
      ],
          // Accommodate binary protobuf data
          stdoutEncoding: null);
      if (result.exitCode != 0) {
        print(result.stdout);
        print(result.stderr);
        throw Exception('Failed to inspect ${blob.buildPath} '
            '(debug file: ${debugElf.absolute.path})');
      }
      // Save stdout as flatbuffers
      final output =
          options.build.openFile('${blob.buildPath}.bloaty_report_pb');
      await output.writeAsBytes(result.stdout, flush: true);
      options.jobIterationCallback?.call();
    }));
  }
  await Future.wait(allFutures);
  options.jobCompleteCallback?.call();
}
