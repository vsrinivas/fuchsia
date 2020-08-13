// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:convert';
import 'dart:io';
import 'package:dart_strict_deps_lib/file_processor.dart' as file_processor;
import 'package:meta/meta.dart';
import 'package:dart_strict_deps_lib/dependency_check.dart' as dependency_check;
import 'package:args/args.dart';
import 'package:collection/collection.dart';
import 'package:path/path.dart' as p;
import 'package:tools.dart-strict-deps.dart_strict_deps_proto/protos/models.pb.dart';

import 'package:package_config/package_config.dart';

p.Context context = p.Context(style: p.Style.posix);

/// Writes serialized TargetCheckResult to file
void writeOutfile(String file, TargetCheckResult checkResult) {
  File(file).writeAsString(jsonEncode(checkResult.toProto3Json()));
}

// Returns string [s] with [indents] number of spaces in front.
String indentedString(String s, {@required int indents}) {
  return s.padLeft(s.length + indents, ' ');
}

/// Represents a flattened out structure for the parsed data.
///
/// This is used for easier formatting of output.
class FlatResult {
  // File path of the importing file
  final String filePath;
  // Import statement being represented
  final ImportResult importResult;
  // Whether the package of import has been imported or not
  final bool knownPackage;
  FlatResult(this.filePath, this.importResult, {this.knownPackage});
}

/// Prints file level warnings to stdout.
void printFileWarnings(String fileName, List<FlatResult> flatResults) {
  final fileState = flatResults[0].importResult.state;
  if (fileState == ImportResult_State.MISSING_FROM_PACKAGE_SOURCES) {
    print(indentedString(
        'File `$fileName` was not found in package sources list. Used here:',
        indents: 2));
  } else if (fileState == ImportResult_State.FILE_MISSING) {
    print(indentedString(
        'File `$fileName` does not exist on the filesystem. Used here:',
        indents: 2));
  } else {
    print(indentedString('File `$fileName` could not be found. Used here:',
        indents: 2));
  }
  for (var importer in flatResults) {
    print(indentedString(
        'Line ${importer.importResult.lineInfo}: ${importer.filePath}',
        indents: 4));
  }
}

/// Prints package level warnings to stdout.
void printPackageWarnings(MapEntry packageResult) {
  Map<String, List<FlatResult>> byFile = groupBy(packageResult.value,
      (packageResult) => packageResult.importResult.resolvedLocation);
  if (packageResult.value[0].importResult.isDartPackageImported) {
    print(
        'Package ${packageResult.key} imported directly and errors found with files imported');
  } else if (packageResult.value[0].knownPackage) {
    print(
        'Package ${packageResult.key} imported transitively and errors found with files imported from it:');
  } else {
    print(
        'Package ${packageResult.key} was not a GN dep but files were imported from it:');
  }
  byFile.forEach(printFileWarnings);
}

/// Finds and prints warnings to stdout.
void printWarnings(
    {TargetCheckResult targetCheckResult,
    String outFile,
    PackageConfig resolver}) {
  final knownPackages =
      resolver.packages.map((package) => package.name).toList();

  final flatResults = targetCheckResult.files.expand((file) => file.imports.map(
      (importResult) => FlatResult(file.filePath, importResult,
          knownPackage: knownPackages.contains(importResult.dartPackage))));

  final notFoundResults = flatResults
      .where((result) => result.importResult.state != ImportResult_State.FOUND);

  final notFoundByPackage =
      groupBy(notFoundResults, (result) => result.importResult.dartPackage);

  notFoundByPackage.entries.forEach(printPackageWarnings);
  if (notFoundByPackage.entries.isNotEmpty) {
    exitCode = -1;
  }
}

void main(List<String> args) async {
  var parser = ArgParser()
    ..addOption('metadata-file', help: 'collected metadata file from GN')
    ..addOption('output-file', help: 'output file to write result to')
    ..addOption('packages-file',
        help: 'packages file to read dependency info from');
  var parsedArgs = parser.parse(args);
  String inputFile = parsedArgs['metadata-file'];
  String outFile = parsedArgs['output-file'];
  String packagesFile = parsedArgs['packages-file'];
  PackageConfig resolver = await loadPackageConfig(File(packagesFile).absolute);
  String inputFileContents = File(inputFile).readAsStringSync();
  final targetCheckResult = dependency_check.checkBuildInfo(
      buildInfo: file_processor.buildInfoFromJson(inputFileContents),
      resolver: resolver);
  printWarnings(
      targetCheckResult: targetCheckResult,
      outFile: outFile,
      resolver: resolver);
  writeOutfile(outFile, targetCheckResult);
}
