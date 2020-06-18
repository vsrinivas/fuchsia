// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:io';

import 'package:args/args.dart';
import 'package:fxtest/fxtest.dart';
import 'package:meta/meta.dart';
import 'package:pedantic/pedantic.dart';

/// Translator for command line arguments into [FuchsiaTestCommand] primitives.
class FuchsiaTestCommandCli {
  /// Callable that prints help/usage information for when the user passes
  /// invalid arguments or "--help".
  final Function(ArgParser) usage;

  /// Fully-hydrated object containing answers to every runtime question.
  /// Derivable from the set of raw arguments passed in by the user.
  TestsConfig testsConfig;

  /// The underlying class which does all the work.
  FuchsiaTestCommand _cmd;

  /// Used to create any new directories needed to house test output / artifacts.
  final DirectoryBuilder directoryBuilder;

  final FuchsiaLocator _fuchsiaLocator;

  FuchsiaTestCommandCli(
    List<String> rawArgs, {
    @required this.usage,
    FuchsiaLocator fuchsiaLocator,
    this.directoryBuilder,
  }) : _fuchsiaLocator = fuchsiaLocator ?? FuchsiaLocator.shared {
    testsConfig = TestsConfig.fromRawArgs(
      rawArgs: rawArgs,
      // When running real tests, turn on logging. Passing `--no-log` explicitly
      // will still override this.
      // The `null` value is not a falsy indicator - it is because `--log` is a
      // flag and thus does not accept a value.
      defaultRawArgs: {'--log': null},
      fuchsiaLocator: _fuchsiaLocator,
    );
  }

  Future<bool> preRunChecks(
    String fxLocation,
    Function(Object) stdoutWriter,
  ) async {
    if (testsConfig.testArguments.parsedArgs['help']) {
      usage(fxTestArgParser);
      return false;
    }
    if (testsConfig.testArguments.parsedArgs['printtests']) {
      ProcessResult result = await Process.run('cat', ['tests.json'],
          workingDirectory: _fuchsiaLocator.buildDir);
      stdoutWriter(result.stdout);
      return false;
    }

    // This command uses extensive fx re-entry, so it's good to make sure fx
    // is actually located where we expect
    final fxFile = File(fxLocation);
    if (!fxFile.existsSync()) {
      throw MissingFxException();
    }

    return true;
  }

  Future<void> run() async {
    _cmd = createCommand();

    // Without waiting, start the command.
    unawaited(
      // But register a listener for when it completes, which resolves the
      // stdout future.
      _cmd.runTestSuite(TestsManifestReader()).then((_) {
        // Once the actual command finishes without problems, close the stdout.
        _cmd.dispose();
      }),
    );

    // Register a listener for when the `stdout` closes.
    try {
      await Future.wait(
        _cmd.outputFormatters.map((var f) => f.stdOutClosedFuture),
        eagerError: true,
      );
    } on Exception {
      if (exitCode == 0) {
        rethrow;
      } else {
        throw OutputClosedException(exitCode);
      }
    }
  }

  FuchsiaTestCommand createCommand() => FuchsiaTestCommand.fromConfig(
        testsConfig,
        directoryBuilder: directoryBuilder,
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
