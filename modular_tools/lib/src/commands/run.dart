// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:async';
import 'dart:io';

import 'package:path/path.dart' as path;

import '../base/file_system.dart';
import '../base/process.dart';
import '../binary_fetcher.dart';
import '../build.dart';
import '../configuration.dart';
import '../index.dart';
import '../run_args.dart';
import 'modular_command.dart';
import 'package:modular_tools/src/inspector_web_server.dart';

void validateInt(String param, String value) {
  if (value == null) {
    throw new Exception('$param cannot be null');
  }
  if (int.parse(value, onError: (_) => null) == null) {
    throw new Exception('$param must be an integer');
  }
}

class RunCommand extends ModularCommand {
  @override
  String get name => 'run';
  @override
  String get description =>
      'Run this module using other modules / components from the CDN.';

  RunCommand() {
    argParser.addOption('recipe',
        help: 'Source relative path of a recipe to run');
    argParser.addOption('module', help: 'URL of a module to run by itself');
    argParser.addOption('session', help: 'session-id to run');
    argParser.addFlag('debugger',
        negatable: false, help: 'Run with mojo debugger');
    argParser.addFlag('trace-startup',
        negatable: false, help: 'Run with startup tracing enabled');
    argParser.addFlag('inspector',
        negatable: false, help: 'Run the modular inspector.');
    argParser.addOption('trace-startup-duration',
        defaultsTo: '10',
        help: 'Duration of the startup tracing in seconds',
        callback: (final String value) =>
            validateInt('trace-startup-duration', value));
    argParser.addOption('mojo-origin',
        help: 'Origin from where to ephimerally download mojo applications.');
    argParser.addOption('local-flutter-engine',
        help: 'Path to a local checkout of flutter/engine with `flutter.mojo` '
            'to use.');
    argParser.addOption('local-modular-binaries',
        help: 'Path to a local build checkout of domokit/modular\'s out '
            'directory.');
    argParser.addOption('session-data',
        allowMultiple: true,
        help:
            'JSON data to add to the session before start-up. For debugging.');
    argParser.addFlag('watch', help: 'Display session data as it changes.');
    argParser.addFlag('dry-run',
        abbr: 'n',
        negatable: false,
        help: 'Only show what would be run, but do not execute anything.');

    addMojoOptions(argParser);
  }

  Future<int> runModular() async {
    final argsForMojo = [];
    final argsForHandler = [];

    if (target == TargetPlatform.android) {
      argsForMojo.add('--android');
    }

    if (release) {
      argsForMojo.add('--release');
    } else {
      argsForMojo.add('--debug');
      argsForMojo.add('--args-for=mojo:flutter --enable-checked-mode');
    }

    if (argResults['debugger']) {
      argsForMojo.add('--debugger');
    }

    if (argResults['trace-startup']) {
      argsForMojo.add('--trace-startup');
      final String duration = argResults['trace-startup-duration'];
      argsForMojo.add('--trace-startup-duration=$duration');
      argsForMojo
          .add('--args-for=mojo:dart_content_handler --complete-timeline');
    }

    if (argResults.wasParsed('mojo-origin')) {
      final String mojoOrigin = argResults['mojo-origin'];
      argsForMojo.add('--origin=$mojoOrigin');
    }

    argsForMojo.addAll(getMojoArgs(argResults, target, false));

    // Configure Flutter. TODO(ppi): to make it less brittle we should call
    // `flutter_tools` with a special flag to get the artifact path from it.
    final flutterPlatform = targetPlatformToArchString[target];
    final flutterArtifactsPath = argResults.wasParsed('local-flutter-engine')
        ? path.absolute(argResults['local-flutter-engine'])
        : path.join(environment.flutterRoot, 'bin', 'cache', 'artifacts',
            'engine', flutterPlatform);
    argsForMojo.add('--map-origin=http://flutter/=$flutterArtifactsPath');
    argsForMojo.add('--url-mappings=mojo:flutter=http://flutter/flutter.mojo');
    if (!release) {
      argsForMojo.add('--args-for=mojo:flutter --enable-checked-mode');
    }
    // Copy flutter.mojo in the build directory so that we get the correct
    // version of flutter.mojo when developing examples with a local checkout
    // of Modular.
    final File flutterBinary =
        new File(path.join(flutterArtifactsPath, 'flutter.mojo'));
    await flutterBinary.copy(path.join(environment.buildDir, 'flutter.mojo'));

    // TODO(ianloic): ensure that at most one of --recipe, --session & --module
    // were supplied.

    // Configure the mojo app to be launched.
    final handlerURL = 'https://tq.mojoapps.io/handler.mojo';
    if (argResults.wasParsed('recipe')) {
      final recipePath = path.absolute(argResults['recipe']);
      final recipeDir = path.current;
      // The recipe file must be under the location from which modular run is
      // invoked. We assume for now that all its relative imports are under that
      // location too (otherwise the handler will fail parsing the recipe).
      assert(path.isWithin(recipeDir, recipePath));
      final recipeFile = path.relative(recipePath, from: recipeDir);
      argsForMojo.add('--map-origin=http://recipe/=$recipeDir');
      argsForMojo.add('mojo:launcher http://recipe/$recipeFile');
    } else if (argResults.wasParsed('session')) {
      final session = argResults['session'];
      argsForMojo.add('mojo:launcher $handlerURL?session=$session');
    } else if (argResults.wasParsed('module')) {
      final module = argResults['module'];
      argsForMojo.add('mojo:launcher $handlerURL?module=$module');
    } else {
      argsForMojo.add('mojo:launcher $handlerURL?root_session=true');
    }

    if (argResults['watch']) {
      argsForHandler.add('--watch');
    }

    if (argResults.wasParsed('session-data')) {
      argsForHandler.addAll(argResults['session-data']
          .map((String data) => '--session-data=$data'));
    }

    if (argResults['inspector']) {
      // Forward the debug web server port over ADB if running on Android.
      if (target == TargetPlatform.android) {
        int adbResult =
            await process('adb', ['forward', 'tcp:1842', 'tcp:1842']);
        if (adbResult != 0) {
          throw new Exception("Couldn't forward port 1842 for the inspector.");
        }
      }
      // Ask the handler to start the debug server.
      argsForHandler.add('--inspector');

      // TODO(ianloic): fail if the user hasn't run `bower install`
      // Run a local web server to server the static web app.
      InspectorWebServer ws = new InspectorWebServer(
          '${environment.modularRoot}', environment, release);
      ws.run();
    }

    if (argsForHandler.length > 0) {
      argsForMojo.add('--args-for=$handlerURL ' + argsForHandler.join(' '));
    }

    argsForMojo.add('--mojo-version=${environment.mojoRevision}');
    // TODO(alhaad); A mojo-devtools config file is required for now when
    // developing out-of-repo. This could be automated.
    final String mojoDevtoolsConfigPath =
        path.join(environment.base, 'mojoconfig');
    argsForMojo.add('--config-file=$mojoDevtoolsConfigPath');

    final command = path.join(environment.devtoolsPath, 'mojo_run');

    if (argResults['dry-run']) {
      final quotedArgs = argsForMojo.map((a) => "'$a'");
      print('$command \\\n  ${quotedArgs.join(" \\\n  ")}');
      return 0;
    }

    return process(command, argsForMojo);
  }

  @override
  Future<int> runInProject() async {
    await ensureDirectoryExists(environment.buildDir, isDirPath: true);
    final List<Step> parallelSteps = [
      new BuildRunner(environment, release).runBuild,
      new IndexGenerator(environment).generateIndex
    ];
    // We assume that if ninja is present then the binary artifacts are to be
    // built locally. This assumption may change as the set of binary artifcacts
    // changes.
    if (!environment.isModularRepo) {
      parallelSteps.add(_getBinaryArtifacts);
    }
    return pipeline([() => parallel(parallelSteps), runModular]);
  }

  Future<int> _getBinaryArtifacts() async {
    if (argResults.wasParsed('local-modular-binaries')) {
      return new BinaryFetcher(environment, target).fetchBinaries(
          localSourceDirectory: argResults['local-modular-binaries']);
    }
    return new BinaryFetcher(environment, target).fetchBinaries();
  }
}
