// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:async';
import 'dart:io';

import 'package:args/args.dart';
import 'package:path/path.dart' as path;

import '../lib/index.dart';
import '../lib/local_files.dart';
import '../lib/render_html.dart';
import '../lib/render_json.dart';

/// Prints the usage help for the script.
void printUsage(final ArgParser parser) {
  print('Indexes local manifests and recipes.');
  print('');
  print('usage: run [options] [directories to index]');
  print('');
  print('options:');
  print('');
  print(parser.usage);
  print('');
}

/// Returns the directory object for the given path as long as it exists, raises
/// an exception otherwise.
Directory getDirectoryIfExists(final String path) {
  final Directory directory = new Directory(path);
  if (!directory.existsSync()) {
    throw new ArgumentError('\'$path\' is not a directory or doesn\'t exist');
  }
  return directory;
}

/// Writes the json index file to the output directory.
Future<File> writeIndexAsJson(final Index index, final Directory outDir) {
  File outputFile = new File(path.join(outDir.path, 'index.json'));
  return outputFile.writeAsString(renderJsonIndex(index));
}

/// Writes the json types file to the output directory.
Future<File> writeTypesAsJson(final Index index, final Directory outDir) {
  File outputFile = new File(path.join(outDir.path, 'types.json'));
  return outputFile.writeAsString(renderJsonTypes(index));
}

/// Writes the html index file to the output directory.
Future<File> writeIndexAsHtml(final Index index, final Directory outDir) {
  File outputFile = new File(path.join(outDir.path, 'index.html'));
  return outputFile.writeAsString(renderHtmlIndex(index));
}

/// Runs the indexer pipeline.
Future<Null> main(final List<String> args) async {
  final ArgParser parser = new ArgParser();
  parser.addFlag('help', help: 'Print usage and exit.');
  parser.addOption('host',
      defaultsTo: '', help: 'Base host for the generated urls.');
  parser.addOption('host-root',
      defaultsTo: '.',
      help: 'Root directory to which indexed files are relative');
  parser.addOption('output-directory', help: 'Directory to store the index in');

  final ArgResults parsedArgs = parser.parse(args);
  if (parsedArgs.rest.isEmpty || parsedArgs['help']) {
    printUsage(parser);
    exit(-1);
  }

  Directory hostRoot = getDirectoryIfExists(parsedArgs['host-root']);
  Directory outputDir = getDirectoryIfExists(parsedArgs['output-directory']);

  List<Directory> directories =
      parsedArgs.rest.map((final String path) => getDirectoryIfExists(path));

  final Index index = new Index();
  final String mojoHost =
      parsedArgs['host'].replaceFirst('https://', 'mojo://');
  await indexLocalFiles(index, directories, hostRoot, mojoHost);

  // Write the index in each output format.
  await writeIndexAsJson(index, outputDir);
  await writeTypesAsJson(index, outputDir);

  await writeIndexAsHtml(index, outputDir);
}
