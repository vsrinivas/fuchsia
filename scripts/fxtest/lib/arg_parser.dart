// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:args/args.dart';
import 'package:fxutils/fxutils.dart';

/// Test-friendly wrapper so unit tests can use `fxTestArgParser` without
/// having its environment-aware print statements blow up.
String getFriendlyBuildDir() {
  var buildDir = '//out/default';
  try {
    buildDir = FxEnv(
      envReader: EnvReader.fromEnvironment(),
    ).userFriendlyOutputDir;

    // ignore: avoid_catching_errors
  } on RangeError {
    // pass
  }
  return buildDir;
}

final ArgParser fxTestArgParser = ArgParser()
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
  ..addMultiOption('package',
      abbr: 'p',
      help: 'Matches tests against their Fuchsia package Name',
      splitCommas: false)
  ..addMultiOption('component',
      abbr: 'c',
      help: '''Matches tests against their Fuchsia component Name. When
--package is also specified, results are filtered both by package
and component.''',
      splitCommas: false)
  ..addMultiOption('and',
      abbr: 'a',
      help: '''When present, adds additional requirements to the preceding
`testName` filter''')
  ..addFlag('printtests',
      defaultsTo: false,
      negatable: false,
      help: 'If true, prints the contents of '
          '`${getFriendlyBuildDir()}/tests.json`')
  ..addFlag('build',
      defaultsTo: true,
      negatable: true,
      help: 'If true, invokes `fx build` before running the test suite')
  ..addFlag('restrict-logs',
      defaultsTo: true,
      negatable: true,
      help: 'If true, passes a flag of the same name to the component test '
          'runner')
  ..addFlag('updateifinbase',
      defaultsTo: true,
      negatable: true,
      help:
          'If true, invokes `fx update-if-in-base` before running device tests')
  ..addFlag('use-package-hash',
      defaultsTo: true,
      negatable: true,
      help:
          'If true, uses the package Merkle root hash from the build artifacts '
          'when executing device tests')
  ..addFlag('info',
      defaultsTo: false,
      negatable: false,
      help: 'If true, prints the test specification in key:value format, '
          'and exits')
  ..addFlag('random',
      abbr: 'r',
      defaultsTo: false,
      negatable: false,
      help: 'If true, randomizes test execution order')
  ..addOption('fuzzy',
      defaultsTo: '3',
      help: 'The Levenshtein distance threshold to use '
          'when generating suggestions')
  ..addFlag('dry',
      defaultsTo: false,
      negatable: false,
      help: 'If true, does not invoke any tests')
  ..addFlag('fail',
      abbr: 'f',
      defaultsTo: false,
      negatable: false,
      help: 'If true, halts test suite execution on the first failed test')
  ..addFlag('log',
      defaultsTo: false,
      negatable: true,
      help: '''If true, emits all output from all tests to a file. Turned on
when running real tests unless `--no-log` is passed.''')
  ..addOption('logpath',
      defaultsTo: null,
      help: '''If passed and if --no-log is not passed, customizes the
destination of the log artifact.

Defaults to a timestamped file at the root of ${getFriendlyBuildDir()}.''')
  ..addOption('limit',
      defaultsTo: null,
      help: 'If passed, ends test suite execution after N tests')
  ..addOption('slow',
      defaultsTo: '2',
      abbr: 's',
      help: '''When set to a non-zero value, triggers output for any test that
takes longer than N seconds to execute.

Note: This has no impact if the -o flag is also set.
Note: The -s flag used to be an abbreviation for --simple.''')
  ..addOption('realm',
      abbr: 'R',
      defaultsTo: null,
      help: '''If passed, runs the tests in a named realm instead of a
randomized one.''')
  ..addOption('min-severity-logs',
      help: '''Filters log output to only messages with this for device tests.
Valid severities: TRACE, DEBUG, INFO, WARN, ERROR, FATAL.''')
  ..addFlag('exact',
      defaultsTo: false,
      help: 'If true, does not perform any fuzzy-matching on tests')
  ..addFlag('e2e',
      defaultsTo: false,
      help: 'If true, allows the execution of host tests that require a '
          'connected device or emulator, such as end-to-end tests.')
  ..addFlag('only-e2e',
      defaultsTo: false,
      help: 'If true, skips all non-e2e tests. The `--e2e` flag is redundant '
          'when passing this flag.')
  ..addFlag('skipped',
      defaultsTo: false,
      negatable: false,
      help: '''If true, prints a debug statement about each skipped test.

Note: The old `-s` abbreviation now applies to `--simple`.''')
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
      help: '''If true, will reduce unsupported tests to a warning and continue
executing. This is dangerous outside of the local development
cycle, as "unsupported" tests are likely a problem with this
command and not the tests.''')
  ..addFlag('verbose', abbr: 'v', defaultsTo: false, negatable: false);
