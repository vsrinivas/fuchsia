// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:io';

import 'package:args/args.dart';
import 'package:fxtest/fxtest.dart';
import 'package:io/ansi.dart';
import 'package:meta/meta.dart';

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

  FuchsiaTestCommandCli(this.rawArgs, {@required this.usage})
      : passThroughTokens = FuchsiaTestCommandCli.extractPassThroughTokens(
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

  static ArgResults parseArgs(List<String> rawArgs, Function(ArgParser) usage) {
    var localArgs = FuchsiaTestCommandCli.splitArgs(rawArgs)[0];

    try {
      return fxTestArgParser.parse(localArgs);
    } on Exception catch (ex) {
      stderr.writeln('Invalid syntax: $ex');
      usage(fxTestArgParser);
      exitCode = 64; // Improper usage
      return null;
    }
  }

  Future<bool> preRunChecks() async {
    if (parsedArgs['help']) {
      usage(fxTestArgParser);
      return false;
    }
    if (parsedArgs['printtests']) {
      ProcessResult result = await Process.run('cat', ['tests.json'],
          workingDirectory: FuchsiaLocator.shared.buildDir);
      stdout.write(result.stdout);
      return false;
    }
    return true;
  }

  Future<void> run() async {
    bool shouldRun = await preRunChecks();
    if (!shouldRun) return;

    var testNamesCollector = TestNamesCollector([
      parsedArgs['testNames'],
      parsedArgs.rest,
    ]);

    var testsConfig = TestsConfig.fromArgResults(
      results: parsedArgs,
      passThroughTokens: passThroughTokens,
      testNames: testNamesCollector.collect(),
    );

    var slowTestThreshold = testsConfig.flags.warnSlowerThan > 0
        ? Duration(seconds: testsConfig.flags.warnSlowerThan)
        : null;
    var formatter = testsConfig.flags.isVerbose
        ? VerboseOutputFormatter(
            slowTestThreshold: slowTestThreshold,
            shouldColorizeOutput: testsConfig.flags.simpleOutput,
            shouldShowPassedTestsOutput: testsConfig.flags.allOutput,
          )
        : CondensedOutputFormatter(
            slowTestThreshold: slowTestThreshold,
            shouldColorizeOutput: testsConfig.flags.simpleOutput,
          );
    _cmd = FuchsiaTestCommand(
      fuchsiaLocator: FuchsiaLocator.shared,
      outputFormatter: formatter,
      testsConfig: testsConfig,
    );

    if (testsConfig.flags.shouldRebuild) {
      formatter.update(TestInfo(wrapWith('> fx build', [green, styleBold])));
      await rebuildFuchsia();
    }
    exitCode = await _cmd.runTestSuite();
    return await _cmd.cleanUp();
  }

  Future<void> terminateEarly() async {
    _cmd?.emitEvent(AllTestsCompleted());
  }

  Future<void> cleanUp() async {
    await _cmd?.cleanUp();
  }
}
