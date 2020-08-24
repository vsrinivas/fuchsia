// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// @dart = 2.8

import 'dart:async';
import 'dart:collection';
import 'dart:convert';
import 'dart:core';
import 'dart:io';

import 'package:crypto/crypto.dart' show sha256;
import 'package:crypto/src/digest.dart';
import 'package:pool/pool.dart';
import 'package:path/path.dart' as path;
import 'package:googleapis/discovery/v1.dart' as discovery;

import 'package:codesize/codesize.dart';
import 'package:codesize/cli.dart' as cli;
import 'package:codesize/io.dart' as io;

import 'progress_bar.dart';

/// The common location for storing Fuchsia debug symbols. See fxbug.dev/41031.
const _fxSymbolCache = '.fuchsia/debug/symbol-cache';

/// Entry point of our application from the command line.
Future<void> main(List<String> args) =>
    runWithIo<io.Standard, void>(() => mainImpl(args));

Future<void> mainImpl(List<String> args) async {
  final parsedArgs = cli.parseArgs(args);
  if (parsedArgs == null) return;

  // The high-level flow of the program:
  // - Generate bloaty reports if they aren't already cached.
  // - Run requested queries on those reports.
  // - Present the results in the specified format.
  try {
    final cs = CodeSize(parsedArgs.buildDir);

    AnalysisRequest allBloatyReportFiles = await ensureReportFiles(cs,
        cachingBehavior: parsedArgs.cachingBehavior,
        heatmap: parsedArgs.heatmap);

    List<Query> populatedQueries = await runQueriesOnReports(
        parsedArgs.selectedQueries,
        parsedArgs.concurrency,
        parsedArgs.onlyLang,
        parsedArgs.fileRegex,
        allBloatyReportFiles,
        cs);

    await presentResults(
        parsedArgs.format, parsedArgs.output, populatedQueries);
    // ignore: avoid_catches_without_on_clauses
  } catch (e) {
    // Generic top-level exception handler
    final io = Io.get();
    io.err.writeln('''
════════════════════════════════════════════════
Oops, `fx codesize` has crashed.
Did you use the correct product when building?
(e.g. check the `Product:` line in `fx status`)
The tool is only supported on shipping products.

In case of actual bug, please file a Monorail
with component `Tools>codesize`.
════════════════════════════════════════════════

Detailed exception below.'''
        .trim());
    rethrow;
  }
}

class ParseManifestResult {
  /// List of `meta.far` files indicating packages.
  final List<File> packageFars;

  /// A mapping from Merkel roots to blob information.
  final Map<String, Blob> blobsByHash;

  ParseManifestResult(this.packageFars, this.blobsByHash);
}

class CodeSize {
  CodeSize(String buildDir) : build = Build(buildDir);

  final Build build;

  /// List of packages in the build.
  final List<Package> packages = <Package>[];

  /// A mapping from BuildIds to build artifacts with that BuildId.
  /// There might be multiple artifacts with the same BuildId. For instance,
  /// the same binary might appear in both fuchsia.zbi and zedboot.zbi.
  HashMap<String, SplayTreeSet<BuildArtifact>> artifactsByBuildId =
      HashMap<String, SplayTreeSet<BuildArtifact>>();

  /// A mapping from BuildIds to lists of files with that BuildId.
  final HashMap<String, SplayTreeSet<File>> debugBinaries =
      HashMap<String, SplayTreeSet<File>>();

  /// A mapping from BuildIds to link map files.
  final HashMap<String, File> buildIdToLinkMapFile = HashMap<String, File>();

  /// The set of BuilIds for which we have obtained a debug binary.
  final Set<String> matchedDebugBinaries = <String>{};

  // Callbacks used to control the progress bar.
  void Function(int) jobInitCallback;
  void Function() jobIterationCallback;
  void Function() jobCompleteCallback;

  /// I/O operations implementation.
  final Io io = Io.get();

  Future<ParseManifestResult> parseManifest(File manifestFile) async {
    final List<File> packageFars = <File>[];
    final Map<String, Blob> blobsByHash = {};

    final manifestDir = manifestFile.parent;
    final lines = await manifestFile.readAsLines();
    // Format of the manifest:
    // HASH = RELATIVE PATH
    for (final line in lines) {
      final parts = line.split('=');
      final hash = parts[0];
      // Path entries are specified relative to the directory
      // containing the manifest.
      final entryPath = build.rebasePath((manifestDir / File(parts[1])).path);
      final file = build.openFile(entryPath);
      if (entryPath.endsWith('meta.far')) {
        // Blobs ending with `meta.far` indicate there is a
        // corresponding package.
        packageFars.add(file);
      }

      final stat = file.statSync();
      if (stat.type == FileSystemEntityType.notFound) {
        throw Exception('$entryPath does not exist');
      }
      if (blobsByHash.containsKey(hash)) {
        // Duplicate blob entry.
        io.err.writeln(
            'Duplicate blob entry: $hash -> ${build.rebasePath(entryPath)} '
            'and ${blobsByHash[hash].buildPath}');
        continue;
      }
      blobsByHash[hash] = await createBlob(hash, entryPath, stat);
    }
    return ParseManifestResult(
        packageFars.toList(growable: false), blobsByHash);
  }

  Future<Blob> createBlob(String hash, String entryPath, FileStat stat) async {
    final blob = Blob()
      ..hash = hash
      ..buildPath = build.rebasePath(entryPath)
      ..sizeOnHost = stat.size
      ..count = 0;

    // Extract contents as subBlobs if the blob is a Zircon Boot Image.
    if (entryPath.endsWith('.zbi.signed')) {
      blob.subBlobs = await extractZbi(entryPath);
    }
    return blob;
  }

  /// Extracts the ZBI inside the build directory and returns the contents
  /// as a list of `SubBlob`s.
  Future<List<SubBlob>> extractZbi(String entryPath) async {
    // Signed ZBI cannot be processed directly by the zbi tool.
    // Use the unsigned counterpart.
    final unsignedZbi =
        build.openFile(entryPath.substring(0, entryPath.length - 7));
    final bootfsDir = build.openDirectory(
        'obj/codesize/bootfs-${unsignedZbi.path.split('/').last}');
    if (bootfsDir.existsSync()) {
      // Clear previously extracted directory.
      await bootfsDir.delete(recursive: true);
    }
    await bootfsDir.create(recursive: true);
    print('Extracting ${build.rebasePath(unsignedZbi.path)}');
    // Run the zbi host tool.
    // out/default/host_x64/zbi --extract --output-dir ~/vg/out/tmp out/default/fuchsia.zbi
    final result = await io.processManager.run([
      build.openFile('host_x64/zbi').path,
      '--extract',
      '--output-dir',
      bootfsDir.absolute.path,
      unsignedZbi.absolute.path
    ],
        workingDirectory: build.dir.path,
        stdoutEncoding: const AsciiCodec(allowInvalid: true),
        stderrEncoding: const AsciiCodec(allowInvalid: true));
    if (result.exitCode != 0) {
      print('stdout: ${result.stdout}');
      print('stderr: ${result.stderr}');
      throw Exception('Failed to extract zbi for $unsignedZbi');
    }
    final files = bootfsDir.list(recursive: true, followLinks: false);
    final subBlobs = <SubBlob>[];
    await for (final file in files) {
      if (file is File) {
        final subBlob = SubBlob()
          ..name =
              path.relative(path.normalize(file.path), from: bootfsDir.path)
          ..buildPath = build.rebasePath(file.path)
          ..sizeOnHost = (file.statSync()).size;
        subBlobs.add(subBlob);
      }
    }
    return subBlobs;
  }

  Future<void> addBlobSizes(File path, Map<String, Blob> blobsByHash) async {
    final blobs = json.decode(await path.readAsString());
    for (var blob in blobs) {
      final b = blobsByHash[blob['merkle']];
      b?.size = blob['size'];
    }
  }

  String metaFarPathToBlobsJson(File far) {
    // Assumes details of //build/package.gni, namely that it generates
    //   <build-dir>/.../<package>/meta.far
    // and puts a blobs.json file into
    //   <build-dir>/.../<package>/blobs.json
    if (!far.path.endsWith('/meta.far')) {
      throw Exception('Build details have changed');
    }
    final jsonPath = "${removeSuffix(far.path, 'meta.far')}blobs.json";
    if (!build.openFile(jsonPath).existsSync()) {
      throw Exception('Build details have changed - '
          'path to blobs.json $jsonPath not found for ${far.path}');
    }
    return jsonPath;
  }

  Future<void> computePackagesInParallel(
      ParseManifestResult manifest, int jobs) async {
    final tasks = <Future>[];
    for (var i = 0; i < jobs; i++) {
      tasks.add(computePackages(manifest));
    }
    await Future.wait(tasks);
  }

  Future<void> computePackages(ParseManifestResult manifest) async {
    final packageFars = manifest.packageFars.toList();
    while (packageFars.isNotEmpty) {
      File far = packageFars.removeLast();

      var package = Package()..path = far.path;
      var parts = package.path.split('/');
      package
        ..name = maybeRemoveSuffix(
            parts.length > 1 ? parts[parts.length - 2] : parts.last, '.meta')
        ..size = 0
        ..private = 0
        ..blobCount = 0
        ..blobsByPath = <String, Blob>{};

      var blobs = json.decode(
          await build.openFile(metaFarPathToBlobsJson(far)).readAsString());

      for (var blob in blobs) {
        var hash = blob['merkle'];
        var blobPath = build.rebasePath(blob['path']);
        var b = manifest.blobsByHash[hash];
        if (b == null) {
          throw Exception('$blobPath $hash is in a package manifest '
              'but not the final manifest');
        }
        b.count++;
        var sourcePath = build.openFile(blob['source_path']).path;
        // If the source_path looks like <something>/blobs/<merkle>, it from a
        // prebuilt package and has no meaningful source. Instead, use the path
        // within the package as its identifier.
        if (sourcePath.endsWith('/blobs/$hash')) {
          sourcePath = 'pkg:${package.name}/${build.rebasePath(blobPath)}';
        } else {
          sourcePath = build.rebasePath(sourcePath);
        }
        // We may see the same blob referenced from different packages with
        // different source paths. If all references agree with each other, use
        // that. Otherwise we would print the first observed path and
        // append " *" to denote that the path is only one of many.
        b.sourcePaths.add(sourcePath);
        package.blobsByPath[blobPath] = b;
      }

      packages.add(package);
    }
  }

  void computeStats(Map<String, Blob> blobsByHash) {
    blobsByHash.forEach((hash, blob) {
      if (blob.count == 0) {
        throw Exception('${blob.hash} is in the final manifest '
            'but not any package manifest');
      }
    });

    for (var package in packages) {
      for (var blob in package.blobsByPath.values) {
        package.size += blob.size;
        if (blob.count == 1) {
          package.private += blob.size;
        }
        package.blobCount++;
      }
    }
  }

  Future<void> addBuildIdFromBlobsInImage(Map<String, Blob> blobsByHash) async {
    final artifacts = blobsByHash.values.expand((blob) {
      return <BuildArtifact>[blob] + blob.subBlobs;
    });
    artifactsByBuildId = await readBuildId(
        artifacts,
        (BuildArtifact art) => build.openFile(art.buildPath),
        Platform.numberOfProcessors * 2);
  }

  Future<void> addBuildIdFromSymbolSources() async {
    final fuchsiaBuildDir = build.openDirectory('.build-id');
    final Directory zirconBuildDir =
        build.zirconBuildDirectory() / Directory('.build-id');
    final sourceTree = Directory(Platform.environment['FUCHSIA_DIR']).absolute;
    final home = Directory(Platform.environment['HOME']);
    final maybeCipdBuildIdDirs = ([
              'prebuilt/.build-id',
              'prebuilt/third_party/clang/linux-x64/lib/debug/.build-id',
              'prebuilt/third_party/clang/mac-x64/lib/debug/.build-id',
            ].map((e) => sourceTree / Directory(e)).toList() +
            [
              home / Directory(_fxSymbolCache),
            ])
        .where((e) => e.existsSync());
    await _addBuildId(Stream.fromIterable(
        [fuchsiaBuildDir, zirconBuildDir, ...maybeCipdBuildIdDirs]));
  }

  Future<void> _addBuildId(Stream<Directory> dirs) async {
    final debugFiles = <File>[];
    final contents = dirs.asyncExpand((dir) {
      if (!dir.existsSync()) {
        throw Exception('Cannot find ${dir.absolute}');
      }
      return dir.list(recursive: true, followLinks: false).where(
          (entity) => (entity is File) && entity.path.endsWith('.debug'));
    });
    await for (final entity in contents) {
      if (entity is File) {
        debugFiles.add(entity);
      }
    }
    debugBinaries.addAll(await readBuildId(
        debugFiles, (x) => x, Platform.numberOfProcessors * 2,
        compare: (File a, File b) => a.path.compareTo(b.path)));
  }

  void matchDebugBinaries() {
    for (final buildId in debugBinaries.keys) {
      if (artifactsByBuildId.containsKey(buildId)) {
        matchedDebugBinaries.add(buildId);
      }
    }
  }

  Future<HashMap<String, SplayTreeSet<T>>> readBuildId<T>(
      Iterable<T> items, File Function(T) toFile, int jobs,
      {int compare(T a, T b)}) async {
    jobInitCallback?.call(items.length);
    final exp = RegExp(
        r'(.*): ELF 64-bit LSB .*ARM aarch64,.*BuildID(.*)=([a-f0-9]+),');
    final buildIdToBlob = HashMap<String, SplayTreeSet<T>>();
    final pool = Pool(jobs);
    final allFutures = <Future>[];
    for (final blob in items) {
      final file = toFile(blob);
      allFutures.add(pool.withResource(() async {
        final result =
            await io.processManager.run(['file', file.absolute.path]);
        if (result.exitCode != 0) {
          print(result.stdout);
          print(result.stderr);
          throw Exception('Failed to inspect $file');
        }
        final matches = exp.allMatches(result.stdout);
        if (matches.isNotEmpty) {
          if (matches.length != 1) {
            throw Exception('Unexpected matches on `file $file`');
          }
          final match = matches.first;
          // Fuchsia uses [xxHash]; Chromium uses [sha1]
          final buildIdAlgo = match.group(2);
          if (buildIdAlgo != '[xxHash]' &&
              buildIdAlgo != '[sha1]' &&
              buildIdAlgo != '[md5/uuid]') {
            throw Exception(
                'Unexpected BuildId algo `$buildIdAlgo` on `file $file`');
          }
          final buildId = match.group(3);
          buildIdToBlob.putIfAbsent(buildId, () => SplayTreeSet<T>(compare));
          buildIdToBlob[buildId].add(blob);
        }
        jobIterationCallback?.call();
      }));
    }
    await Future.wait(allFutures);
    jobCompleteCallback?.call();
    return buildIdToBlob;
  }

  // Finds all `.map` files in the build directory, and find the corresponding
  // BuildID of the unstripped binary that is next to it.
  Future<void> collectLinkMapFiles() async {
    final files = build.dir.list(recursive: true, followLinks: false).where(
        (file) =>
            file.path.endsWith('.map') &&
            file.statSync().type == FileSystemEntityType.file);
    final elfFiles = <String>[];
    await for (var mapFile in files) {
      final elfFile =
          build.openFile(mapFile.path.substring(0, mapFile.path.length - 4));
      if (!elfFile.existsSync()) continue;
      elfFiles.add(elfFile.path);
    }
    final buildIds = await readBuildId(
        elfFiles, build.openFile, Platform.numberOfProcessors * 2);
    for (final entry in buildIds.entries) {
      buildIdToLinkMapFile[entry.key] =
          build.openFile('${entry.value.first}.map');
    }
  }
}

Future<AnalysisRequest> ensureReportFiles(CodeSize cs,
    {cli.CachingBehavior cachingBehavior, File heatmap}) async {
  AnalysisRequest allBloatyReportFiles;
  final bloatyStamp = cs.build.openFile('codesize_bloaty_report.stamp');
  if (bloatyStamp.existsSync()) {
    bool useCache;
    switch (cachingBehavior) {
      case cli.CachingBehavior.alwaysUseCache:
        print('Explicitly using cached report files');
        useCache = true;
        break;
      case cli.CachingBehavior.neverUseCache:
        print('Ignoring cached report files');
        useCache = false;
        break;
      case cli.CachingBehavior.useIfUpToDate:
        // Use the cached reports if no newer full build was performed.
        final manifestStat = cs.build.blobManifestFile().statSync();
        final stampStat = bloatyStamp.statSync();
        useCache = stampStat.modified.isAfter(manifestStat.modified);
        if (useCache) {
          // Check if heatmap file remained the same too.
          final lastRequest = AnalysisRequest.fromJson(
              json.decode(await bloatyStamp.readAsString()));
          if (lastRequest.heatmapContentSha != null) {
            if (heatmap == null) {
              useCache = false;
            } else {
              final digest = await _hashFile(heatmap);
              if (digest != lastRequest.heatmapContentSha) {
                useCache = false;
              }
            }
          } else {
            if (heatmap != null) {
              useCache = false;
            }
          }
          if (!useCache) {
            print('Not using cached report files since '
                'the heatmap option has changed');
          }
        } else {
          print('Not using cached report files since '
              'a new full build has been performed');
        }
        if (useCache) {
          print('Using cached report files since '
              'it was generated after the last full build');
        }
        break;
    }
    if (useCache) {
      // Skipping running bloaty; load `allBloatyReportFiles` from stamp
      allBloatyReportFiles = AnalysisRequest.fromJson(
          json.decode(await bloatyStamp.readAsString()));
      print('Loaded ${allBloatyReportFiles.items.length} reports from cache');
      return allBloatyReportFiles;
    }
  }

  // Rerun bloaty and save the report file index as a stamp.
  try {
    allBloatyReportFiles =
        await generateBloatyReportsFromBuild(cs, heatmap: heatmap);
    await bloatyStamp.writeAsString(json.encode(allBloatyReportFiles.toJson()));
  } finally {
    await GoogleApiClient.close();
  }
  return allBloatyReportFiles;
}

Future<String> _hashFile(File heatmap) async =>
    sha256.convert(await heatmap.readAsBytes()).toString();

Future<List<Query>> runQueriesOnReports(
    List<QueryThunk> queries,
    int concurrency,
    SourceLang onlyLang,
    RegExp fileRegex,
    AnalysisRequest allBloatyReportFiles,
    CodeSize cs) async {
  print('Running these queries: ${queries.map((s) => s().name).join(', ')}');
  final queryRunner =
      QueryRunner(queries, numConcurrency: concurrency, onlyLang: onlyLang);
  final filteredReports = allBloatyReportFiles.items
      .where((report) => fileRegex.hasMatch(report.name));
  print(
      'Analyzing ${filteredReports.length} reports whose names match the ${fileRegex.pattern} regex');
  await Future.wait([
    for (final reportFile in filteredReports)
      queryRunner.addReport(reportFile.absolute(cs.build.dir.path))
  ]);
  await queryRunner.join();
  return queryRunner.queries;
}

Future<void> presentResults(cli.OutputFormat outputFormat, IOSink output,
    List<Query> populatedQueries) async {
  final io = Io.get();
  // Data presentation
  Renderer renderer;
  switch (outputFormat) {
    case cli.OutputFormat.basic:
      renderer = BasicRenderer();
      break;
    case cli.OutputFormat.html:
      renderer = HtmlRenderer();
      break;
    case cli.OutputFormat.terminal:
      renderer = TerminalRenderer(supportsControlCharacters: output == io.out);
      break;
    case cli.OutputFormat.tsv:
      renderer = TsvRenderer();
      break;
  }

  try {
    renderer.render(output, populatedQueries);
  } finally {
    await output.flush();
    if (output != io.out) {
      await output.close();
    }
  }
}

Future<AnalysisRequest> generateBloatyReportsFromBuild(CodeSize cs,
    {File heatmap}) async {
  final allBloatyReportFiles = AnalysisRequest(
      items: [], heatmapContentSha: await flatMap(heatmap, _hashFile));
  final io = Io.get();

  final manifest = await cs.parseManifest(cs.build.blobManifestFile());
  final blobsByHash = manifest.blobsByHash;
  await cs.addBlobSizes(cs.build.openFile('blobs.json'), blobsByHash);
  await cs.computePackagesInParallel(manifest, Platform.numberOfProcessors);
  cs.computeStats(blobsByHash);

  ProgressBar progress;
  cs
    ..jobInitCallback = ((max) => progress = ProgressBar(complete: max))
    ..jobIterationCallback = (() => progress.update(progress.current + 1))
    ..jobCompleteCallback = (() => progress.done());

  io.out.write('Loading link maps ');
  await cs.collectLinkMapFiles();

  io.out.write('Loading BuildId for blobs in image ');
  await cs.addBuildIdFromBlobsInImage(blobsByHash);
  final elfBlobSizes = cs.artifactsByBuildId.values
      .map((e) => e.first.sizeOnHost)
      .reduce((v, e) => v + e);
  final allBlobSizes =
      blobsByHash.values.map((e) => e.sizeOnHost).reduce((v, e) => v + e);
  print('${cs.artifactsByBuildId.length} (${formatSize(elfBlobSizes)}) out of '
      '${blobsByHash.length} (${formatSize(allBlobSizes)}) blobs are '
      'ELF binaries');

  io.out.write('Loading BuildId from symbol sources ');
  await cs.addBuildIdFromSymbolSources();
  print('${cs.debugBinaries.length} debug binaries found locally');

  cs.matchDebugBinaries();
  final matchedBinarySizes = cs.matchedDebugBinaries
      .map((k) => cs.artifactsByBuildId[k].first.sizeOnHost)
      .reduce((v, e) => v + e);
  print('${cs.matchedDebugBinaries.length} (${formatSize(matchedBinarySizes)})'
      ' out of ${cs.artifactsByBuildId.length} (${formatSize(elfBlobSizes)})'
      ' ELF binaries have matching debug info');

  print('Binaries without matching debug info:');
  for (var buildId in cs.artifactsByBuildId.keys) {
    if (!cs.matchedDebugBinaries.contains(buildId)) {
      final buildPath = cs.artifactsByBuildId[buildId].first.buildPath;
      print('  ${cs.build.rebasePath(buildPath)}');
      print('  - BuildId: $buildId');
      if (blobsByHash.containsKey(buildPath.split('/').last)) {
        final sourcePath = blobsByHash[buildPath.split('/').last].sourcePaths;
        print('  - Source Path: $sourcePath');
      }
    }
  }

  print('Querying symbol servers');
  final gotAny = await downloadUnmatchedDebugBinaries(cs);
  if (gotAny) {
    await cs.addBuildIdFromSymbolSources();
    cs.matchDebugBinaries();
  } else {
    print('Did not find any extra symbols from symbol servers');
  }

  var buildIds = <String>{};
  HashMap<String, String> buildIdToAccessPattern;
  if (heatmap != null) {
    buildIdToAccessPattern = HashMap<String, String>();
    final HashMap<String, String> merkleToAccessPattern =
        HashMap<String, String>();
    for (final line in await heatmap.readAsLines()) {
      final firstComma = line.indexOf(',');
      final merkle = line.substring(0, firstComma);
      final accessPattern = line.substring(firstComma + 1);
      merkleToAccessPattern[merkle] = accessPattern;
    }
    for (final entry in cs.artifactsByBuildId.entries) {
      final buildId = entry.key;
      final artifact = entry.value.first;
      if (artifact is Blob) {
        if (cs.matchedDebugBinaries.contains(buildId) &&
            merkleToAccessPattern.containsKey(artifact.hash)) {
          final accessPattern = merkleToAccessPattern[artifact.hash];
          // Exclude fully-hot blobs, since downstream queries would be confused
          // as to which language this ELF is written in due to lack of symbols.
          final int elfSize =
              cs.build.openFile(artifact.buildPath).statSync().size;
          const int frameSize = 32 * 1024;
          final int numFrames = (elfSize / frameSize).ceil();
          final isFrameHot = List<bool>.generate(numFrames, (i) => false);
          for (final part in accessPattern.split(',')) {
            final frameAndCount = part.split(':').toList();
            if (frameAndCount.length != 2) {
              throw Exception('Invalid access pattern $accessPattern');
            }
            final frame = int.parse(frameAndCount[0]);
            final count = int.parse(frameAndCount[1]);
            if (frame >= numFrames) {
              throw Exception('Blob merkle ${artifact.hash} '
                  '(at: ${artifact.buildPath}) is $elfSize bytes on disk, '
                  'but access pattern indicated an out-of-range frame $frame. '
                  'Does the heatmap CSV match the version of the build?');
            }
            if (count > 0) isFrameHot[frame] = true;
          }
          if (isFrameHot.every((e) => e)) continue;

          buildIds.add(buildId);
          buildIdToAccessPattern[buildId] = accessPattern;
        }
      }
    }

    io.out.writeln('Blob access heatmap specified, only looking at '
        '${buildIds.length} files with both cold regions and debug info');
    for (final buildId in buildIds) {
      io.out.writeln('-> ${cs.artifactsByBuildId[buildId].first}');
    }
  } else {
    buildIds = cs.matchedDebugBinaries;
  }

  io.out.write('Running bloaty on matched binaries ');
  await runBloatyOnMatchedBinaries(buildIds,
      options: RunBloatyOptions(
        build: cs.build,
        artifactsByBuildId: cs.artifactsByBuildId,
        debugBinaries: cs.debugBinaries,
        buildIdToLinkMapFile: cs.buildIdToLinkMapFile,
        buildIdToAccessPattern: buildIdToAccessPattern,
        jobInitCallback: cs.jobInitCallback,
        jobIterationCallback: cs.jobIterationCallback,
        jobCompleteCallback: cs.jobCompleteCallback,
      ));

  final zbiExtractedBinary = RegExp(r'obj/codesize/bootfs-(.*)\.zbi/(.*)$');
  for (final buildId in buildIds) {
    final blob = cs.artifactsByBuildId[buildId].first;
    var name = blob.buildPath;
    if (blob is SubBlob) {
      final match = zbiExtractedBinary.firstMatch(blob.buildPath);
      if (match != null)
        name = '[zbi: ${match.group(1)}.zbi] /${match.group(2)} '
            '(at: ${cs.build.rebasePath(blob.buildPath)})';
    } else if (blob is Blob) {
      final formattedBuildPath = cs.build.rebasePath(blob.buildPath);
      final prebuiltDueToPkgHash = blob.sourcePaths
          .fold(true, (prev, element) => prev && element.startsWith('pkg:'));
      final prebuiltInPrebuiltFolder =
          formattedBuildPath.startsWith('../../prebuilt/');
      if (prebuiltDueToPkgHash || prebuiltInPrebuiltFolder) {
        name = '[prebuilt] '
            '${blob.sourcePaths.first}'
            '${blob.sourcePaths.length > 1 ? "* " : ""} '
            '(at: $formattedBuildPath)';
      } else {
        final maybeNonPkgPrebuilt =
            blob.sourcePaths.where((e) => !e.startsWith('pkg:')).first;
        if (maybeNonPkgPrebuilt != null) {
          name = maybeNonPkgPrebuilt;
        } else {
          name = formattedBuildPath;
        }
      }
    }
    // If heatmap was specified, we'd generate two versions of reports, one of
    // which is filtered using the heatmap.
    final report = AnalysisItem(
        path: cs.build.rebasePath('${blob.buildPath}.bloaty_report_pb'),
        filteredCounterpart: heatmap != null
            ? cs.build.rebasePath('${blob.buildPath}.filtered.bloaty_report_pb')
            : null,
        name: name);
    if (!cs.build.openFile(report.path).existsSync())
      throw Exception('Could not find bloaty report at ${report.path}');
    allBloatyReportFiles.items.add(report);
  }

  return allBloatyReportFiles;
}

Future<bool> downloadUnmatchedDebugBinaries(CodeSize cs) async {
  final homeDir = Directory(Platform.environment['HOME']);
  final repos = await Future.wait([
    'gs://fuchsia-artifacts/debug',
    'gs://fuchsia-artifacts-internal/debug',
    'gs://fuchsia-artifacts-release/debug',
  ].map(
      (e) => CloudRepo.create(e, Cache(homeDir / Directory(_fxSymbolCache)))));
  var downloadedAny = false;
  for (var buildId in cs.artifactsByBuildId.keys) {
    if (!cs.matchedDebugBinaries.contains(buildId)) {
      var downloaded = false;
      for (var repo in repos) {
        try {
          await repo.getBuildObject(buildId);
          // A 404 error will be expected if the debug symbol is not found.
          // ignore: avoid_catching_errors
        } on discovery.DetailedApiRequestError catch (err) {
          if (err.status != 404) {
            rethrow;
          }
          continue;
        }
        print('Downloaded $buildId');
        downloaded = true;
        break;
      }
      if (!downloaded) {
        final buildPath =
            cs.build.rebasePath(cs.artifactsByBuildId[buildId].first.buildPath);
        print('Did not find $buildId ($buildPath) in symbol servers');
      } else {
        downloadedAny = true;
      }
    }
  }
  return downloadedAny;
}
