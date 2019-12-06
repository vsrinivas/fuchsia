// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:async';
import 'dart:io';

import 'package:args/args.dart';
import 'package:fxtest/fxtest.dart';
import 'package:io/ansi.dart';

void usage(ArgParser parser) {
  stdout
    ..writeln(wrapWith(
      'Warning! This is an experimental new test command.',
      [magenta],
    ))
    ..writeln()
    ..writeln('Usage: fx test [testName ...]')
    ..writeln()
    ..writeln(
      'The value(s) supplied for `testName` can be fully-formed Fuchsia '
      'Package URLs, Fuchsia package names, or Fuchsia-tree directories. '
      'Partial tree paths will execute all descendent tests.',
    )
    ..writeln()
    ..writeln('Options:')
    ..writeln(parser.usage);
}

/// CLI-flavored wrapper for [FuchsiaTestCommand]
Future<void> main(List<String> args) async {
  var fuchsiaLocator = FuchsiaLocator();

  final parser = ArgParser()
    ..addMultiOption('testNames', abbr: 't')
    ..addFlag('help', abbr: 'h', defaultsTo: false, negatable: false)
    ..addFlag('host',
        defaultsTo: false,
        negatable: false,
        help: 'If true, only runs host tests. The opposite of `--device`')
    ..addFlag('device',
        abbr: 'd',
        defaultsTo: false,
        negatable: false,
        help: 'If true, only runs device tests. The opposite of `--host`')
    ..addFlag('printtests',
        defaultsTo: false,
        negatable: false,
        help: 'If true, prints the contents of '
            '`${fuchsiaLocator.userFriendlyBuildDir}/tests.json`')
    ..addFlag('random',
        abbr: 'r',
        defaultsTo: false,
        negatable: false,
        help: 'If true, randomizes test execution order')
    ..addFlag('dry',
        defaultsTo: false,
        negatable: false,
        help: 'If true, does not invoke any tests')
    ..addFlag('fail',
        abbr: 'f',
        defaultsTo: false,
        negatable: false,
        help: 'If true, halts test suite execution on the first failed test')
    ..addOption('limit',
        defaultsTo: null,
        help: 'If passed, ends test suite execution after N tests')
    ..addOption('warnslow',
        defaultsTo: null,
        help: 'If passed, prints a debug message for each test that takes '
            'longer than N seconds to execute')
    ..addFlag('skipped',
        abbr: 's',
        defaultsTo: false,
        negatable: false,
        help: 'If true, prints a debug statement about each skipped test')
    ..addFlag('simple',
        defaultsTo: false,
        negatable: false,
        help: 'If true, removes any color or decoration from output')
    ..addFlag('output',
        abbr: 'o',
        defaultsTo: false,
        negatable: false,
        help: 'If true, also displays the output from passing tests')
    ..addFlag('silenceunsupported',
        abbr: 'u',
        defaultsTo: false,
        negatable: false,
        help: 'If true, will reduce unsupported tests to a warning and '
            'continue executing.\nThis is dangerous outside of the local '
            'development cycle, as "unsupported"\ntests are likely a problem '
            'with this command, not the tests.')
    ..addFlag('verbose', abbr: 'v', defaultsTo: false, negatable: false);

  List<String> passThroughTokens = [];
  if (args.contains('--')) {
    // Starting at the <position of '--' + 1> to avoid capturing '--' itself
    passThroughTokens = args.sublist(args.indexOf('--') + 1);
    // ignore: parameter_assignments
    args = args.sublist(0, args.indexOf('--'));
  }

  ArgResults argResults;
  try {
    argResults = parser.parse(args);
  } on Exception catch (ex) {
    stderr.writeln('Invalid syntax: $ex');
    usage(parser);
    exitCode = 64; // Improper usage
    return;
  }

  if (argResults['help']) {
    usage(parser);
    return;
  }

  if (argResults['printtests']) {
    ProcessResult result = await Process.run(
      'cat',
      ['tests.json'],
      workingDirectory: fuchsiaLocator.buildDir,
    );
    stdout.write(result.stdout);
    return;
  }

  var testNamesCollector = TestNamesCollector([
    argResults['testNames'],
    argResults.rest,
  ]);

  var testFlags = TestFlags(
    allOutput: argResults['output'],
    dryRun: argResults['dry'],
    isVerbose: argResults['verbose'] || argResults['output'],
    limit: int.parse(argResults['limit'] ?? '0'),
    passThroughTokens: passThroughTokens,
    simpleOutput: !argResults['simple'],
    shouldFailFast: argResults['fail'],
    shouldOnlyRunDeviceTests: argResults['device'],
    shouldOnlyRunHostTests: argResults['host'],
    shouldPrintSkipped: argResults['skipped'],
    shouldRandomizeTestOrder: argResults['random'],
    shouldSilenceUnsupported: argResults['silenceunsupported'],
    testNames: testNamesCollector.collect(),
    warnSlowerThan: int.parse(argResults['warnslow'] ?? '0'),
  );

  var slowTestThreshold = testFlags.warnSlowerThan > 0
      ? Duration(seconds: testFlags.warnSlowerThan)
      : null;
  var formatter = testFlags.isVerbose
      ? VerboseOutputFormatter(
          slowTestThreshold: slowTestThreshold,
          shouldColorizeOutput: testFlags.simpleOutput,
          shouldShowPassedTestsOutput: testFlags.allOutput,
        )
      : CondensedOutputFormatter(
          slowTestThreshold: slowTestThreshold,
          shouldColorizeOutput: testFlags.simpleOutput,
        );
  var cmd = FuchsiaTestCommand(
    fuchsiaLocator: fuchsiaLocator,
    outputFormatter: formatter,
    testFlags: testFlags,
  );

  try {
    exitCode = await cmd.runTestSuite();
  } on Exception catch (err) {
    stdout.write(wrapWith(err.toString(), [red]));
    // ignore: cascade_invocations
    stdout.writeln('');
    exitCode = 2;
  } finally {
    // Close all streams
    cmd.dispose();
  }
}
