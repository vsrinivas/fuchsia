// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:io';

import 'package:args/args.dart';
import 'package:fxtest/fxtest.dart';
import 'package:io/ansi.dart';
import 'package:meta/meta.dart';
import 'package:pedantic/pedantic.dart';

/// Translator for command line arguments into [FuchsiaTestCommand] primitives.
class FuchsiaTestCommandCli {
  /// Callable that prints help/usage information for when the user passes
  /// invalid arguments or "--help".
  final Function(ArgParser) usage;

  /// Fully-hydrated object containing answers to every runtime question.
  /// Derivable from the set of raw arguments passed in by the user.
  TestsConfig _testsConfig;

  /// The underlying class which does all the work.
  FuchsiaTestCommand _cmd;

  final FuchsiaLocator _fuchsiaLocator;

  FuchsiaTestCommandCli(
    List<String> rawArgs, {
    @required this.usage,
    FuchsiaLocator fuchsiaLocator,
  }) : _fuchsiaLocator = fuchsiaLocator ?? FuchsiaLocator.shared {
    _testsConfig = TestsConfig.fromRawArgs(
      rawArgs: rawArgs,
      fuchsiaLocator: _fuchsiaLocator,
    );
  }

  Future<bool> preRunChecks(
    String fxLocation,
    Function(Object) stdoutWriter,
  ) async {
    if (_testsConfig.testArguments.parsedArgs['help']) {
      usage(fxTestArgParser);
      return false;
    }
    if (_testsConfig.testArguments.parsedArgs['printtests']) {
      ProcessResult result = await Process.run('cat', ['tests.json'],
          workingDirectory: FuchsiaLocator.shared.buildDir);
      stdoutWriter(result.stdout);
      return false;
    }

    // This command uses extensive fx re-entry, so it's good to make sure fx
    // is actually located where we expect
    var fxFile = File(fxLocation);
    if (!fxFile.existsSync()) {
      throw MissingFxException();
    }

    return true;
  }

  Future<void> run() async {
    _cmd = buildCommand(_testsConfig);

    if (_testsConfig.flags.shouldRebuild) {
      _cmd.outputFormatter.update(
        TestInfo(wrapWith('> fx build', [green, styleBold])),
      );
      await rebuildFuchsia(FuchsiaLocator.shared.fx);
    }

    // Without waiting, start the command.
    unawaited(
      // But register a listener for when it completes, which resolves the
      // stdout future.
      _cmd.runTestSuite(TestsManifestReader()).then((_) {
        // Once the actual command finishes without problems, close the stdout.
        _cmd.outputFormatter.close();
      }),
    );

    // Register a listener for when the `stdout` closes.
    await _cmd.outputFormatter.stdOutClosedFuture.catchError((err) {
      // If we have not yet determined a failing error code, then go with
      // whatever is baked into this error.
      if (exitCode == 0) {
        throw err;
      } else {
        // However, if we do already have an `exitCode` (from a failing test),
        // use that
        throw OutputClosedException(exitCode);
      }
    });
  }

  FuchsiaTestCommand buildCommand(TestsConfig testsConfig) =>
      FuchsiaTestCommand.fromConfig(
        testsConfig,
        testRunnerBuilder: (TestsConfig testsConfig) => SymbolizingTestRunner(
          fx: testsConfig.fuchsiaLocator.fx,
        ),
      );

  Future<void> terminateEarly() async {
    _cmd?.emitEvent(AllTestsCompleted());
  }

  Future<void> cleanUp() async {
    await _cmd?.cleanUp();
  }
}
