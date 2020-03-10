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
  /// Raw string arguments received from the command line.
  final List<String> rawArgs;

  /// Arguments hydrated into their respective data type representations
  /// ([bool], [String], [int], etc).
  final ArgResults parsedArgs;

  /// Raw string arguments to be forwarded down to each executed test.
  ///
  /// These tokens have no impact whatsoever on how `fx test` finds, filters,
  /// and invokes tests. They are exclusively used by the underlying tests.
  final List<String> passThroughTokens;

  /// Callable that prints help/usage information for when the user passes
  /// invalid arguments or "--help".
  final Function(ArgParser) usage;

  /// The underlying class which does all the work.
  FuchsiaTestCommand _cmd;

  FuchsiaTestCommandCli(
    this.rawArgs, {
    @required this.usage,
  })  : passThroughTokens = FuchsiaTestCommandCli.extractPassThroughTokens(
          rawArgs,
        ),
        parsedArgs = FuchsiaTestCommandCli.parseArgs(rawArgs, usage);

  /// Splits a list of command line arguments into the half intended for
  /// local use and the half intended to be passed through to sub-commands.
  static List<List<String>> splitArgs(List<String> rawArgs) {
    var dashDashIndex = rawArgs.indexOf('--');
    if (dashDashIndex == -1) {
      dashDashIndex = rawArgs.length;
    }
    return [
      rawArgs.take(dashDashIndex).toList(),
      rawArgs.skip(dashDashIndex + 1).toList(),
    ];
  }

  static List<String> extractPassThroughTokens(List<String> rawArgs) {
    return FuchsiaTestCommandCli.splitArgs(rawArgs)[1];
  }

  static ArgResults parseArgs(
    List<String> rawArgs,
    Function(ArgParser) usage,
  ) {
    var localArgs = FuchsiaTestCommandCli.splitArgs(rawArgs)[0];
    return fxTestArgParser.parse(localArgs);
  }

  Future<bool> preRunChecks(
    ArgResults parsedArgs,
    String fxLocation,
    Function(Object) stdoutWriter,
  ) async {
    if (parsedArgs['help']) {
      usage(fxTestArgParser);
      return false;
    }
    if (parsedArgs['printtests']) {
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
    var testNamesCollector = TestNamesCollector([
      parsedArgs['testNames'],
      parsedArgs.rest,
    ]);

    var testsConfig = TestsConfig.fromArgResults(
      results: parsedArgs,
      passThroughTokens: passThroughTokens,
      testNames: testNamesCollector.collect(),
    );

    var _cmd = buildCommand(testsConfig);

    if (testsConfig.flags.shouldRebuild) {
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

  FuchsiaTestCommand buildCommand(TestsConfig testsConfig) {
    var formatter = getFormatter(testsConfig);
    return FuchsiaTestCommand(
      analyticsReporter: testsConfig.flags.dryRun
          ? AnalyticsReporter.noop()
          : AnalyticsReporter(
              fuchsiaLocator: FuchsiaLocator.shared,
            ),
      fuchsiaLocator: FuchsiaLocator.shared,
      outputFormatter: formatter,
      testsConfig: testsConfig,
    );
  }

  OutputFormatter getFormatter(TestsConfig testsConfig) {
    if (testsConfig.flags.infoOnly) {
      return InfoFormatter();
    }
    var slowTestThreshold = testsConfig.flags.warnSlowerThan > 0
        ? Duration(seconds: testsConfig.flags.warnSlowerThan)
        : null;
    return testsConfig.flags.isVerbose
        ? VerboseOutputFormatter(
            hasRealTimeOutput: testsConfig.flags.allOutput,
            slowTestThreshold: slowTestThreshold,
            shouldColorizeOutput: testsConfig.flags.simpleOutput,
          )
        : CondensedOutputFormatter(
            slowTestThreshold: slowTestThreshold,
            shouldColorizeOutput: testsConfig.flags.simpleOutput,
          );
  }

  Future<void> terminateEarly() async {
    _cmd?.emitEvent(AllTestsCompleted());
  }

  Future<void> cleanUp() async {
    await _cmd?.cleanUp();
  }
}
