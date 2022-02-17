// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TODO(https://fxbug.dev/84961): Fix null safeety and remove this language version.
// @dart=2.9

import 'dart:io';

import 'package:args/args.dart';
import 'package:detect_api_changes/analyze.dart';
import 'package:detect_api_changes/diff.dart';
import 'package:path/path.dart' as p;

void main(List<String> arguments) async {
  var argParser = ArgParser()
    ..addFlag('help', help: 'print out usage.')
    ..addOption('api-name', help: 'name of the api to be analyzed.')
    ..addOption('source-file', help: 'path to the list of sources')
    ..addOption('dot-packages', help: 'path to the .packages file')
    ..addOption('output-file', help: 'location to save new API file')
    ..addOption('golden-api', help: 'location of golden API file')
    ..addOption('fuchsia-root', help: 'root path of the fuchsia repo')
    ..addOption('fuchsia-build-root', help: 'root path of the out directory')
    ..addOption('depfile', help: 'depfile')
    ..addFlag('overwrite',
        defaultsTo: false, help: 'overwrite the golden API file');

  ArgResults argResults;
  try {
    argResults = argParser.parse(arguments);
  } on Exception catch (e) {
    print('Unknown exception: $e');
    print(argParser.usage);
    exit(1);
  }

  String apiName = argResults['api-name'];
  if (apiName == null) {
    print('Missing required flag: api-name');
    exit(1);
  }

  String sourceFile = argResults['source-file'];
  if (sourceFile == null) {
    print('Missing required flag: source-file');
    exit(1);
  }
  List<String> sources = [];
  for (var source in await File(sourceFile).readAsLines()) {
    sources.add(p.canonicalize(source));
  }

  String dotPackages = argResults['dot-packages'];
  if (dotPackages == null) {
    print('Missing required flag: dot-packages');
    exit(1);
  }
  File dotPackagesFile = File(dotPackages);

  String goldenAPIPath = argResults['golden-api'];
  if (goldenAPIPath == null) {
    print('Missing required flag: golden-api');
    exit(1);
  }

  String newAPIPath = argResults['output-file'];
  if (newAPIPath == null) {
    print('Missing required flag: output-file');
    exit(1);
  }

  String fuchsiaRoot = argResults['fuchsia-root'];
  if (fuchsiaRoot == null) {
    print('Missing required flag: fuchsia-root');
    exit(1);
  }

  String fuchsiaBuildRoot = argResults['fuchsia-build-root'];
  if (fuchsiaBuildRoot == null) {
    print('Missing required flag: fuchsia-build-root');
    exit(1);
  }

  String depfilePath = argResults['depfile'];
  if (depfilePath == null) {
    print('Missing required flag: depfile');
    exit(1);
  }

  // Analyze and save the new library.
  var dotPackagesFilePath = dotPackagesFile.absolute.path;
  var dotPackagesFileRelativePath =
      p.relative(dotPackagesFilePath, from: fuchsiaBuildRoot);
  File newAPIFile = File(newAPIPath);
  await newAPIFile.writeAsString(
      await analyzeAPI(apiName, sources + [dotPackagesFilePath]));

  for (var i = 0; i < sources.length; i++) {
    sources[i] = p.relative(sources[i], from: fuchsiaBuildRoot);
  }

  var sourcesText = sources.join(' ');
  var depfileText =
      '$newAPIPath: $goldenAPIPath $sourcesText $dotPackagesFileRelativePath';
  File depfile = File(depfilePath);
  await depfile.writeAsString(depfileText);

  // Load the golden library if it exists, otherwise create it.
  File goldenAPIFile = File(goldenAPIPath);
  if (!goldenAPIFile.existsSync() || argResults['overwrite']) {
    await newAPIFile.copy(goldenAPIPath);
    exit(0);
  }

  // Compare the new library with the golden library.
  String result = diffTwoFiles(
      newAPIFile.absolute.path, goldenAPIFile.absolute.path, fuchsiaRoot);
  if (result != '') {
    print(result);
    exit(1);
  }
}
