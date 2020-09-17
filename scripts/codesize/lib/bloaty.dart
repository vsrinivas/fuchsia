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
  final HashMap<String, String> buildIdToAccessPattern;
  final int heatmapFrameSize;

  final void Function(int) jobInitCallback;
  final void Function() jobIterationCallback;
  final void Function() jobCompleteCallback;

  RunBloatyOptions(
      {this.build,
      this.artifactsByBuildId,
      this.debugBinaries,
      this.buildIdToLinkMapFile,
      this.buildIdToAccessPattern,
      this.heatmapFrameSize,
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

/// Runs bloaty on all binaries in the set `buildIds`, saving their
/// results in report files next to the binaries themselves.
/// `buildIds` should be a set of build-ids.
///
/// If `options.buildIdToLinkMapFile` contains a link map for the corresponding
/// binary, also supply that option to bloaty. Link maps will help bloaty
/// produce much more accurate size breakdowns.
Future<void> runBloatyOnMatchedBinaries(Set<String> buildIds,
    {RunBloatyOptions options}) async {
  final io = Io.get();
  final fuchsiaDir = Directory(Platform.environment['FUCHSIA_DIR']);
  final pathToBloaty = fuchsiaDir /
      Directory('prebuilt/third_party/bloaty/${options.hostPlatform}/bloaty');
  final pool = Pool(Platform.numberOfProcessors);
  final allFutures = <Future>[];
  options.jobInitCallback?.call(buildIds.length);
  for (final buildId in buildIds) {
    final blob = options.artifactsByBuildId[buildId].first;
    final debugElf = options.debugBinaries[buildId].first;

    Future<void> _bloaty(
        {List<String> dataSourceArgs = const [
          '-d',
          'compileunits,symbols',
        ],
        String reportSuffix = '.bloaty_report_pb'}) async {
      final result = await io.processManager.run([
        pathToBloaty.absolute.path,
        '--pb',
        ...dataSourceArgs,
        '-n',
        '0',
        '-s',
        'file',
        '--demangle=full',
        '--debug-file=${debugElf.absolute.path}',
        if (options.buildIdToLinkMapFile.containsKey(buildId))
          '--link-map-file=${options.buildIdToLinkMapFile[buildId].absolute.path}',
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
      // Save stdout as protobuf
      final output = options.build.openFile('${blob.buildPath}$reportSuffix');
      await output.writeAsBytes(result.stdout, flush: true);
    }

    if (options.buildIdToAccessPattern != null) {
      // Generate two reports, one filtered and one unfiltered by access pattern
      allFutures
        ..add(pool
            .withResource(_bloaty)
            .then((x) => options.jobIterationCallback?.call()))
        ..add(pool.withResource(() => _bloaty(dataSourceArgs: [
              '--cold-bytes-filter',
              options.buildIdToAccessPattern[buildId],
              '--access-pattern-frame-size',
              options.heatmapFrameSize.toString(),
              '-d',
              'accesspattern,compileunits,symbols',
            ], reportSuffix: '.filtered.bloaty_report_pb')));
    } else {
      // Only generate the unfiltered report
      allFutures.add(pool
          .withResource(_bloaty)
          .then((x) => options.jobIterationCallback?.call()));
    }
  }
  await Future.wait(allFutures);
  options.jobCompleteCallback?.call();
}
