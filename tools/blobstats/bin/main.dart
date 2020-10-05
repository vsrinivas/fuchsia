// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//  @dart = 2.8

import 'dart:async';
import 'dart:io';

import 'package:args/args.dart';
import 'package:blobstats/blobstats.dart';

ArgResults argResults;
const humanReadable = 'human-readable';
const output = 'output';
const image = 'image';

Future main(List<String> args) async {
  final parser = ArgParser()
    ..addFlag('help', help: 'give this help')
    ..addOption(output, abbr: 'o', help: 'Directory to output report to')
    ..addOption(image, help: 'The image for which to show the stats')
    ..addFlag(humanReadable,
        abbr: 'h',
        defaultsTo: false,
        help: 'Print human readable sizes (e.g., 1K 2M 3G)')
    ..addFlag('dart-packages',
        defaultsTo: false, help: 'Describe duplication of Dart packages');

  argResults = parser.parse(args);
  if (argResults['help']) {
    print('Usage: fx blobstats [OPTION]...\n\nOptions:\n ${parser.usage}');
    return;
  }

  String suffix;
  if (argResults.rest.isNotEmpty) {
    suffix = argResults.rest[0];
  }

  var outputDir = Directory.current;
  if (argResults[output] != null) {
    outputDir = Directory(argResults[output]);
  }

  var stats = BlobStats(Directory.current, outputDir, suffix,
      humanReadable: argResults[humanReadable]);
  var prefix = '';
  if (argResults[image] != null) {
    prefix = '${argResults[image]}_';
  }
  await stats.addManifest('${prefix}obj/build/images', 'blob.manifest');
  await stats.addBlobSizes('${prefix}blobs.json');
  await stats.computePackagesInParallel(Platform.numberOfProcessors);
  stats
    ..computeStats()
    ..printBlobs(40)
    ..printPackages()
    ..printOverallSavings();
  print('');
  await outputDir.create(recursive: true);
  var csvBlobs = await stats.csvBlobs();
  print('Wrote blob data at $csvBlobs');
  var csvPackages = await stats.csvPackages();
  print('Wrote package data at $csvPackages');
  await stats.packagesToChromiumBinarySizeTree();
  if (argResults['dart-packages']) {
    stats.printDartPackages();
  }
}
