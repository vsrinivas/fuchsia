// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:async';
import 'dart:io';

import 'package:args/args.dart';
import 'package:fxtest/fxtest.dart';
import 'package:io/ansi.dart';

void usage(ArgParser parser) {
  stdout.writeln('''Usage: fx test [testName ...]

Options:
${parser.usage}

Examples:

  - Execute all tests
  fx test

  - Execute the test component available at this URL
  fx test fuchsia-pkg://fuchsia.com/myPackageName/#meta/componentName.cmx

  - Execute the test whose package URL's `package-name` component matches
    the value. Runs all tests contained in this package.
  fx test myPackageName

  - Execute the test whose package URL's `resource-path` component matches
    the value. Runs only that test out of its containing package.
  fx test componentName

  - Execute all tests at and below this path (usually host tests)
  fx test //subtree/path

  - Multiple test names can be supplied in the same invocation, e.g.:
  fx test //subtree/path //another/path fuchsia-pkg://...

The value(s) supplied for `testName` can be fully-formed Fuchsia Package URLs,
Fuchsia package names, or Fuchsia-tree directories. Partial tree paths
will execute all descendent tests.
    ''');
}

/// CLI-flavored wrapper for [FuchsiaTestCommand]
Future<void> main(List<String> args) async {
  List<String> passThroughTokens = [];
  if (args.contains('--')) {
    // Starting at the <position of '--' + 1> to avoid capturing '--' itself
    passThroughTokens = args.sublist(args.indexOf('--') + 1);
    // ignore: parameter_assignments
    args = args.sublist(0, args.indexOf('--'));
  }

  ArgResults argResults;
  try {
    argResults = fxTestArgParser.parse(args);
  } on Exception catch (ex) {
    stderr.writeln('Invalid syntax: $ex');
    usage(fxTestArgParser);
    exitCode = 64; // Improper usage
    return;
  }

  if (argResults['help']) {
    usage(fxTestArgParser);
    return;
  }

  if (argResults['printtests']) {
    ProcessResult result = await Process.run(
      'cat',
      ['tests.json'],
      workingDirectory: FuchsiaLocator.shared.buildDir,
    );
    stdout.write(result.stdout);
    return;
  }

  var testNamesCollector = TestNamesCollector([
    argResults['testNames'],
    argResults.rest,
  ]);

  var testsConfig = TestsConfig.fromArgResults(
    results: argResults,
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
  var cmd = FuchsiaTestCommand(
    fuchsiaLocator: FuchsiaLocator.shared,
    outputFormatter: formatter,
    testsConfig: testsConfig,
  );

  registerCleanUp(() async {
    cmd.emitEvent(AllTestsCompleted());
  });
  setUpCleanExit();

  if (testsConfig.flags.shouldRebuild) {
    try {
      formatter.update(TestInfo(wrapWith('> fx build', [green, styleBold])));
      await rebuildFuchsia();
    } on BuildException catch (e) {
      formatter.update(FatalError(e.toString()));
      exitCode = e.exitCode;
      cmd.dispose();
      endSigIntListener();
      return;
    }
  }

  try {
    exitCode = await cmd.runTestSuite();
  } on Exception catch (err) {
    stdout.writeln(wrapWith(err.toString(), [red]));
    exitCode = 2;
  } finally {
    // Close all streams
    cmd.dispose();
    endSigIntListener();
  }
}

StreamSubscription _sigIntSub;

final _cleanup = <Future Function()>[];
void registerCleanUp(Future Function() c) => _cleanup.add(c);

void setUpCleanExit() {
  _sigIntSub ??= ProcessSignal.sigint.watch().listen((signal) async {
    for (var c in _cleanup) {
      await c();
    }
    exitCode = 2;
    exit(exitCode);
    endSigIntListener();
  });
}

void endSigIntListener() {
  _sigIntSub?.cancel();
}
