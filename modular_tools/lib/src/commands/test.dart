// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:async';
import 'dart:convert';
import 'dart:io';

import 'package:path/path.dart' as path;

import '../analyze.dart';
import '../base/file_system.dart';
import '../base/process.dart';
import '../build.dart';
import '../configuration.dart';
import '../dependencies.dart';
import '../index.dart';
import '../run_args.dart';
import 'modular_command.dart';

class TestCommand extends ModularCommand {
  final String name = 'test';
  final String description = 'Test Modular.';

  TestCommand() {
    argParser.addFlag('analyze',
        negatable: true,
        defaultsTo: true,
        help: 'Run analyzer on all packages');
    addMojoOptions(argParser);
  }

  int _handleTestResult(String packagePath, ProcessResult result) {
    if (result.exitCode == 0) {
      stdout.writeln("Tested $packagePath.");
    } else {
      stdout.write("Testing $packagePath... ");
      stdout.writeln("Failed with exit code ${result.exitCode} and output:");
      stdout.writeln("-" * 80);
      stdout.write(result.stdout);
      stderr.write(result.stderr);
      stdout.writeln("-" * 80);
    }
    return result.exitCode;
  }

  Future<int> gnCheck() =>
      processNonBlocking('gn', ['check', environment.buildDir]);

  Future<int> dartTest() async {
    final String pubBinary = path.join(environment.dartSdkPath, 'bin', 'pub');
    final List<String> args = ['run'];
    if (!release) {
      args.add('--checked');
    }
    args.addAll(['test', '-j1']);
    return parallelStream(environment.projectConfigurations,
        (final ProjectConfiguration project) async {
      final Directory testDirectory =
          new Directory(path.join(project.projectRoot, 'test'));
      if (!(await testDirectory.exists())) {
        return 0;
      }
      final StampAndDeps stampAndDeps =
          await getStampAndDeps(environment, project, 'test');
      final File dotPackagesFile =
          new File(path.join(project.projectRoot, '.packages'));
      if (await requiresRebuild(
          stampAndDeps.stamp, stampAndDeps.deps, dotPackagesFile)) {
        int returnValue = await processNonBlockingWithResult(pubBinary, args,
                workingDirectory: project.projectRoot)
            .then((ProcessResult result) =>
                _handleTestResult(project.projectRoot, result));
        if (returnValue == 0) {
          stampAndDeps.stamp.writeAsBytesSync(UTF8.encode(""));
        }
        return returnValue;
      }
      return 0;
    });
  }

  Future<int> gtest() async {
    if (target == TargetPlatform.android) {
      print("Skipped gtests!! Cannot run on android.");
      return 0;
    }

    await for (final File unittest
        in findFilesAndFilter(environment.buildDir, '_unittests', const [])) {
      if (await processNonBlockingWithResult(unittest.path, []).then(
              (ProcessResult result) =>
                  _handleTestResult(unittest.path, result)) !=
          0) {
        return 1;
      }
    }
    return 0;
  }

  Future<int> apptest(final String config) async {
    final List<String> args = [];

    args.add('--mojo-version=${environment.mojoRevision}');

    if (target == TargetPlatform.android) {
      if (!(await _hasAndroidDevice())) {
        print("Skipped apptests!! Connect a device to run them.");
        return 0;
      }
      args.add('--android');
    }

    if (release) {
      args.add('--release');
    } else {
      args.add('--debug');
    }

    args.add(config);
    args.add('--');
    args.addAll(getMojoArgs(argResults, target, true));

    print(path.join(environment.devtoolsPath, 'mojo_test') +
        " " +
        args.join(' '));

    return processNonBlockingWithResult(
            path.join(environment.devtoolsPath, 'mojo_test'), args)
        .then((ProcessResult result) {
      stdout.write(result.stdout);
      stderr.write(result.stderr);
      if (result.stdout.contains("due to shell exit code -11")) {
        stdout.writeln(
            "This is a known issue (domokit/modular#655). Continuing...");
        return 0;
      }
      return result.exitCode;
    });
  }

  Future<bool> _hasAndroidDevice() async {
    final String adb = path.join(environment.modularRoot, 'third_party',
        'android_tools', 'sdk', 'platform-tools', 'adb');
    return processNonBlockingWithResult(adb, ['devices'],
            workingDirectory: null)
        .then((ProcessResult result) =>
            result.exitCode == 0 && result.stdout.contains("\tdevice\n"))
        .catchError((_) => false);
  }

  @override
  Future<int> runInProject() async {
    final List<Step> parallelSteps = [];
    if (argResults['analyze']) {
      parallelSteps.add(new AnalyzerRunner(environment).runAnalyzer);
    }
    parallelSteps.add(new IndexGenerator(environment).generateIndex);
    parallelSteps.add(dartTest);

    final List<Step> serialSteps = [];
    serialSteps.add(new BuildRunner(environment, release).runBuild);
    serialSteps.add(() => parallel(parallelSteps));

    final String apptestConfig = path.join('modular_tools', 'data', 'apptests');
    if (await new File(apptestConfig).exists()) {
      serialSteps.add(() => apptest(apptestConfig));
    }
    return pipeline(serialSteps);
  }
}
