// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:async';
import 'dart:convert';
import 'dart:io';

import 'package:path/path.dart' as path;

import 'base/file_system.dart';
import 'base/process.dart';
import 'configuration.dart';
import 'dependencies.dart';

class AnalyzerRunner {
  final EnvironmentConfiguration _environment;

  AnalyzerRunner(this._environment);

  Future<int> runAnalyzer() =>
      parallelStream(_environment.projectConfigurations, _runAnalyzerInProject);

  Stream<String> _getEntryPoints(ProjectConfiguration project) {
    return findFilesAndFilter(
            project.projectRoot, '.dart', const ['/packages', '/.pub'])
        .map((File f) => path.absolute(f.path));
  }

  int _handleAnalyzerResult(String packagePath, ProcessResult result) {
    if (result.exitCode == 0) {
      stdout.writeln("Analyzed $packagePath.");
    } else {
      stdout.write("Analyzing $packagePath... ");
      stdout.writeln("Failed with exit code ${result.exitCode} and output:");
      stdout.writeln("-" * 80);
      stdout.write(result.stdout);
      stderr.write(result.stderr);
      stdout.writeln("-" * 80);
    }
    return result.exitCode;
  }

  Future<int> _runDartAnalyzer(ProjectConfiguration project) async {
    final String analyzerBinary =
        path.join(_environment.dartSdkPath, 'bin', 'dartanalyzer');
    final String analyzerOptions = path.join(
        _environment.modularRoot, 'modular_tools', 'build', 'analysis_options');
    List<String> args = [
      '--enable_type_checks',
      '--fatal-warnings',
//      '--fatal-hints', // see: https://github.com/domokit/modular/issues/812
      '--options=$analyzerOptions'
    ];
    args.addAll(await _getEntryPoints(project).toList());
    return processNonBlockingWithResult(analyzerBinary, args,
            workingDirectory: project.projectRoot)
        .then((result) => _handleAnalyzerResult(project.projectRoot, result));
  }

  Future<int> _runFlutterAnalyzer(ProjectConfiguration project) async {
    List<String> args = ['analyze', '--no-preamble', '--no-congratulate'];
    args.addAll(await _getEntryPoints(project).toList());
    return processNonBlockingWithResult(
            path.join(_environment.flutterRoot, 'bin', 'flutter'), args,
            workingDirectory: project.projectRoot)
        .then((result) => _handleAnalyzerResult(project.projectRoot, result));
  }

  Future<int> _runAnalyzerInProject(ProjectConfiguration project) async {
    final File analyzerOptionsFile = new File(path.join(
        _environment.modularRoot,
        'modular_tools',
        'build',
        'analysis_options'));
    final File dotPackagesFile =
        new File(path.join(project.projectRoot, '.packages'));
    final StampAndDeps stampAndDeps = await getStampAndDeps(
        _environment, project, 'analyzer',
        extraDeps: [analyzerOptionsFile]);

    int returnValue = 0;
    if (await requiresRebuild(
        stampAndDeps.stamp, stampAndDeps.deps, dotPackagesFile)) {
      if (project.projectType == ProjectType.flutter) {
        // TODO(alhaad): This thing is returning -1 for non-flutter projects.
        // Investigate.
        returnValue = await _runFlutterAnalyzer(project);
      } else {
        returnValue = await _runDartAnalyzer(project);
      }
    }
    if (returnValue == 0) {
      stampAndDeps.stamp.writeAsBytesSync(UTF8.encode(""));
    }
    return returnValue;
  }
}
