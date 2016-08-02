// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:async';
import 'dart:io';

import 'package:args/command_runner.dart';

import 'src/commands/analyze.dart';
import 'src/commands/build.dart';
import 'src/commands/clean.dart';
import 'src/commands/clear_cache.dart';
import 'src/commands/gen.dart';
import 'src/commands/publish.dart';
import 'src/commands/refresh.dart';
import 'src/commands/run.dart';
import 'src/commands/test.dart';
import 'src/configuration.dart';

class ModularToolsCommandRunner extends CommandRunner {
  final EnvironmentConfiguration environment = new EnvironmentConfiguration();

  ModularToolsCommandRunner() : super('modular', 'Build and run modules.') {
    addCommand(new AnalyzeCommand());
    addCommand(new BuildCommand());
    addCommand(new CleanCommand());
    addCommand(new PublishCommand());
    addCommand(new RunCommand());
    addCommand(new TestCommand());

    // These commands are only available when run from within the modular
    // repository.
    // TODO(alhaad): Make these available outside our repository as well.
    if (environment.isModularRepo) {
      addCommand(new ClearCacheCommand());
      addCommand(new GenCommand());
      addCommand(new RefreshCommand());
    }

    argParser.addFlag('verbose',
        abbr: 'v',
        negatable: false,
        help: 'Noisy logging, including all shell commands executed.');

    // Default to Android on MacOS or Linux on other platform (ie: Linux).
    argParser.addOption('target',
        defaultsTo: Platform.isMacOS ? 'android' : 'linux',
        allowed: ['linux', 'android']);

    // Support legacy --android option, but hide it from help.
    argParser.addFlag('android', defaultsTo: false, hide: true);

    argParser.addFlag('release', defaultsTo: false);
  }
}

/// Main entry point for commands.
///
/// This function is intended to be used from the [modular_tools] command line
/// tool.
Future<Null> main(List<String> args) async {
  final CommandRunner runner = new ModularToolsCommandRunner();
  final int exitCode = await runner.run(args);
  // The built-in |help| command returns a null future. We should
  // consider this successful.
  exit(exitCode == null ? 0 : exitCode);
}
