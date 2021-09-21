// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TODO(https://fxbug.dev/84961): Fix null safety and remove this language version.
// @dart=2.9

// @dart = 2.8

import 'dart:async';
import 'dart:io';

import 'package:args/args.dart';
import 'package:fxtest/fxtest.dart';
import 'package:fxutils/fxutils.dart';
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

  - Print help for a test:
  fx test -o testName -- --help

The value(s) supplied for `testName` can be fully-formed Fuchsia Package URLs,
Fuchsia package names, or Fuchsia-tree directories. Partial tree paths
will execute all descendent tests.
    ''');
}

void _directoryBuilder(String path, {bool recursive}) {
  Directory(path).createSync(recursive: recursive);
}

/// CLI entry point for [FuchsiaTestCommand]. The [main] function mostly
/// handles the SIGINT signal, routing through [FuchsiaTestCommandCli] to handle
/// translating raw command line arguments into constructs that make sense to
/// [FuchsiaTestCommand].
Future<void> main(List<String> args) async {
  FuchsiaTestCommandCli cmdCli;
  try {
    cmdCli = FuchsiaTestCommandCli(
      args,
      fxEnv: FxEnv(envReader: EnvReader.fromEnvironment()),
      usage: usage,
      directoryBuilder: _directoryBuilder,
    );
  } on Exception catch (ex) {
    stderr.writeln('Invalid syntax: $ex');
    usage(fxTestArgParser);
    exitCode = 64; // Improper usage
    return null;
  }

  registerCleanUp(() async {
    await cmdCli.terminateEarly();
    await cmdCli.cleanUp();
  });
  _sigintSub ??= ProcessSignal.sigint.watch().listen(cleanUpAndExit);

  try {
    // Before going through all the trouble of initializing everything for the
    // command, make sure everything is valid and that the user doesn't want
    // some simple debug output.
    bool shouldRun = await cmdCli.preRunChecks(
      stdout.write,
    );
    if (shouldRun) {
      // Finally, run the command
      await cmdCli.run();
    }
  } on FxRunException catch (err) {
    stderr.writeln('${wrapWith("Error:", [red])} ${err.toString()}');
    exitCode = err.exitCode;
  } on Exception catch (err) {
    if (err is OutputClosedException) {
      exitCode = err.exitCode;
    } else {
      stderr.writeln(wrapWith(err.toString(), [red]));
    }
  }
  await closeSigIntListener();
  await cmdCli.cleanUp();
}

StreamSubscription _sigintSub;

final _cleanup = <Future Function()>[];
void registerCleanUp(Future Function() c) => _cleanup.add(c);

void cleanUpAndExit(
    [ProcessSignal signal, int _exitCode = failureExitCode]) async {
  // Kick off all registered clean up functions.
  List<Future> cleanUpFutures = _cleanup
      .map((Function cleanUpFunction) async => cleanUpFunction())
      .toList();
  await Future.wait(cleanUpFutures);

  exitCode = _exitCode;
  await closeSigIntListener();
  exit(exitCode);
}

Future<void> closeSigIntListener() async {
  await _sigintSub?.cancel();
}
