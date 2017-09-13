// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:async';
import 'dart:io';

import 'package:analysis_server_lib/analysis_server_lib.dart';
import 'package:args/args.dart';
import 'package:intl/intl.dart';
import 'package:path/path.dart' as path;

/// Runs Dart analysis through the analysis server.
/// This enables effective caching of analysis results.
/// Heavily inspired by:
///   https://github.com/google/tuneup.dart/blob/master/lib/commands/check.dart

// ignore_for_file: unawaited_futures

const String _optionServerSnapshot = 'server-snapshot';
const String _optionSourceDir = 'source-dir';
const String _optionShowResults = 'show-results';
const String _optionCachePath = 'cache-path';
const String _optionStamp = 'stamp';
const String _optionDepName = 'depname';
const String _optionDepFile = 'depfile';
const String _optionPackageRoot = 'package-root';
const List<String> _requiredOptions = const [
  _optionServerSnapshot,
  _optionSourceDir,
  _optionCachePath,
  _optionStamp,
  _optionDepName,
  _optionDepFile,
  _optionPackageRoot,
];

Future<Null> main(List<String> args) async {
  final ArgParser parser = new ArgParser()
    ..addOption(_optionServerSnapshot,
        help: 'Path to the analysis server snapshot')
    ..addOption(_optionPackageRoot, help: 'Path to the package root')
    ..addOption(_optionSourceDir,
        help: 'Path to the source directory relative to the package root')
    ..addFlag(_optionShowResults,
        help: 'Whether to always show results', negatable: true)
    ..addOption(_optionCachePath, help: 'Path to the analysis cache')
    ..addOption(_optionStamp, help: 'Stamp file to update on success')
    ..addOption(_optionDepName, help: 'Name of the target in the dep file')
    ..addOption(_optionDepFile, help: 'Path to the depfile to write');
  final argResults = parser.parse(args);
  if (_requiredOptions
      .any((String option) => !argResults.options.contains(option))) {
    print('Missing option! All options must be specified.');
    exit(1);
  }

  final stopwatch = new Stopwatch()..start();

  final client = await AnalysisServer.create(
    scriptPath: path.canonicalize(argResults[_optionServerSnapshot]),
    clientId: 'Fuchsia Dart build analyzer',
    clientVersion: '0.1',
    serverArgs: ['--cache', path.canonicalize(argResults[_optionCachePath])],
  );

  final completer = new Completer();
  client.processCompleter.future.then((int code) {
    if (!completer.isCompleted) {
      completer.completeError('Analysis exited early (exit code $code)');
    }
  });

  await client.server.onConnected.first.timeout(new Duration(seconds: 10));

  // Handle errors.
  client.server.onError.listen((ServerError e) {
    final trace =
        e.stackTrace == null ? null : new StackTrace.fromString(e.stackTrace);
    completer.completeError(e, trace);
  });

  client.server.setSubscriptions(['STATUS']);
  client.server.onStatus.listen((ServerStatus status) {
    if (status.analysis == null) {
      return;
    }

    if (!status.analysis.isAnalyzing) {
      // Notify that the analysis has finished.
      completer.complete(true);
      client.dispose();
    }
  });

  final errorMap = new Map<String, List<AnalysisError>>();
  client.analysis.onErrors.listen((AnalysisErrors e) {
    errorMap[e.file] = e.errors;
  });

  // Set the path to analyze.
  final sourceDir = argResults[_optionSourceDir];
  final analysisRoot = path.canonicalize(argResults[_optionPackageRoot]);
  final excluded = [];
  if (sourceDir != '.') {
    for (FileSystemEntity entity in new Directory(analysisRoot).listSync()) {
      if (entity is Directory) {
        final name = path.relative(entity.path, from: analysisRoot);
        if (name != sourceDir) {
          excluded.add(entity.path);
        }
      }
    }
  }
  client.analysis.setAnalysisRoots([analysisRoot], excluded);

  // Wait for analysis to finish.
  try {
    await completer.future;
  } catch (error, st) {
    print('$error');
    print('$st');
    exit(1);
  }

  final sources = errorMap.keys.toList();
  final errors = errorMap.values.expand((list) => list).toList();

  // Don't show TODOs.
  errors.removeWhere((e) => e.code == 'todo');

  // Sort by severity, file, offset.
  errors.sort((AnalysisError one, AnalysisError two) {
    final comp = _severityLevel(two.severity) - _severityLevel(one.severity);
    if (comp != 0) {
      return comp;
    }

    if (one.location.file != two.location.file) {
      return one.location.file.compareTo(two.location.file);
    }

    return one.location.offset - two.location.offset;
  });

  if (errors.isNotEmpty) {
    errors.forEach((AnalysisError e) {
      final severity = e.severity.toLowerCase();

      String file = e.location.file;
      if (file.startsWith(analysisRoot)) {
        file = file.substring(analysisRoot.length + 1);
      }
      final location =
          '$file:${e.location.startLine}:${e.location.startColumn}';

      final message = e.message.endsWith('.')
          ? e.message.substring(0, e.message.length - 1)
          : e.message;

      final code = e.code;

      const String bullet = '\u{2022}';

      print('  $severity $bullet $message at $location $bullet ($code)');
    });
  }

  final NumberFormat secondsFormat = new NumberFormat('0.0');
  final seconds = stopwatch.elapsedMilliseconds / 1000.0;
  if (argResults[_optionShowResults]) {
    print('${errors.isEmpty ? "No" : errors.length} '
        'issue${errors.isEmpty ? "" : "(s)"} '
        'found; analyzed ${sources.length} source file(s) '
        'in ${secondsFormat.format(seconds)}s.');
  }

  // Write the depfile.
  final depname = argResults[_optionDepName];
  new File(argResults[_optionDepFile])
      .writeAsStringSync('$depname: ${sources.join(" ")}');

  if (errors.isEmpty) {
    // Write the stamp file.
    new File(argResults[_optionStamp]).writeAsStringSync('Success!');
  }

  exit(errors.isEmpty ? 0 : 1);
}

int _severityLevel(String severity) {
  switch (severity) {
    case 'ERROR':
      return 2;
    case 'WARNING':
      return 1;
    default:
      return 0;
  }
}
