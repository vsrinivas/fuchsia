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
        ).userFriendlyOutputDir ??
        buildDir;

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
      help: 'only run host tests. The opposite of `--device`')
  ..addFlag('device',
      abbr: 'd',
      defaultsTo: false,
      negatable: false,
      help: 'only run device tests. The opposite of `--host`')
  ..addMultiOption('package',
      abbr: 'p',
      help: 'match tests against their Fuchsia package Name',
      splitCommas: false)
  ..addMultiOption('component',
      abbr: 'c',
      help: '''match tests against their Fuchsia component Name. When
--package is also specified, results are filtered both by package
and component.''',
      splitCommas: false)
  ..addMultiOption('and',
      abbr: 'a', help: '''add additional requirements to the preceding
`testName` filter''')
  ..addFlag('printtests',
      defaultsTo: false,
      negatable: false,
      help: 'print the contents of '
          '`${getFriendlyBuildDir()}/tests.json`')
  ..addFlag('build',
      defaultsTo: true,
      negatable: true,
      help: 'invoke `fx build` before running the test suite')
  ..addFlag('restrict-logs',
      defaultsTo: true,
      negatable: true,
      help: 'pass a flag of the same name to the component test '
          'runner')
  ..addFlag('updateifinbase',
      defaultsTo: true,
      negatable: true,
      help: 'invoke `fx update-if-in-base` before running device tests')
  ..addFlag('use-package-hash',
      defaultsTo: true,
      negatable: true,
      help: '''use the package Merkle root hash from the build artifacts
when executing device tests''')
  ..addFlag('info',
      defaultsTo: false,
      negatable: false,
      help: 'print the test specification in key:value format, '
          'and exits')
  ..addFlag('random',
      abbr: 'r',
      defaultsTo: false,
      negatable: false,
      help: 'randomize test execution order')
  ..addOption('fuzzy',
      defaultsTo: '3',
      help: 'the Levenshtein distance threshold to use '
          'when generating suggestions')
  ..addFlag('dry',
      defaultsTo: false, negatable: false, help: 'do not invoke any tests')
  ..addFlag('fail',
      abbr: 'f',
      defaultsTo: false,
      negatable: false,
      help: 'halt test suite execution after the first failed test suite')
  ..addFlag('log',
      defaultsTo: false,
      negatable: true,
      help: '''emit all output from all tests to a file. Turned on
when running real tests unless `--no-log` is passed.''')
  ..addOption('logpath',
      defaultsTo: null,
      help: '''if passed and if --no-log is not passed, customizes the
destination of the log artifact.

Defaults to a timestamped file at the root of ${getFriendlyBuildDir()}.''')
  ..addOption('limit',
      defaultsTo: null, help: 'End test suite execution after N tests')
  ..addOption('slow',
      defaultsTo: '2',
      abbr: 's',
      help: '''when set to a non-zero value, triggers output for any test that
takes longer than N seconds to execute

Note: This has no impact if the -o flag is also set.
Note: The -s flag used to be an abbreviation for --simple.''')
  ..addOption('realm',
      abbr: 'R',
      defaultsTo: null,
      help: '''unused. This flag was relevant for components v1 tests and is
now a no-op. It will soon be removed.''')
  ..addOption('min-severity-logs',
      help: '''filter log output to only messages with this for device tests.
Valid severities: TRACE, DEBUG, INFO, WARN, ERROR, FATAL.''')
  ..addFlag('exact',
      defaultsTo: false, help: 'do not perform any fuzzy-matching on tests')
  ..addFlag('e2e',
      defaultsTo: false,
      help: '''allow execution of host tests that require a connected device
or emulator, such as end-to-end tests.''')
  ..addFlag('only-e2e',
      defaultsTo: false,
      help: 'skip all non-e2e tests. The `--e2e` flag is redundant '
          'when passing this flag.')
  ..addFlag('skipped',
      defaultsTo: false,
      negatable: false,
      help: '''print a debug statement about each skipped test.

Note: The old `-s` abbreviation now applies to `--simple`.''')
  ..addFlag('simple',
      defaultsTo: false,
      negatable: false,
      help: 'remove any color or decoration from output')
  ..addFlag('output',
      abbr: 'o',
      defaultsTo: false,
      negatable: false,
      help: '''display the output from passing tests. Test arguments
may be needed, see 'Print help for a test' example below.''')
  ..addFlag('silenceunsupported',
      abbr: 'u',
      defaultsTo: false,
      negatable: false,
      help: '''reduce unsupported tests to a warning and continue
executing. This is dangerous outside of the local development
cycle, as "unsupported" tests are likely a problem with this
command and not the tests.''')
  ..addMultiOption('test-filter',
      help: '''run specific test cases in v2 suite. Can be specified multiple
times to pass in multiple patterns.
example: --test-filter glob1 --test-filter glob2''',
      splitCommas: false)
  ..addOption('count',
      defaultsTo: null,
      help: '''number of times to run the test. By default run 1 time. If
an iteration of test times out, no further iterations will
be executed.''')
  ..addOption('parallel',
      defaultsTo: null,
      help: '''maximum number of test cases to run in parallel. Overrides any
parallel option set in test specs.''')
  ..addFlag('also-run-disabled-tests',
      defaultsTo: false,
      help: '''run tests that have been marked disabled/ignored by the test
author.''')
  ..addFlag('use-run-test-suite',
      defaultsTo: false,
      help: '''fallback to using run-test-suite instead of ffx test. This option
is a temporary escape hatch.''')
  ..addFlag('show-full-moniker-in-logs',
      defaultsTo: false,
      help: '''shows the full moniker in log output for v2 tests. By default
this is false and ony the last segment of the moniker is displayed.''')
  ..addOption('ffx-output-directory',
      defaultsTo: null,
      help: '''directory to collect test outputs produced by ffx test''')
  ..addOption('timeout',
      defaultsTo: null,
      help: '''test timeout in seconds. The test is killed if not completed when
the timeout has elapsed.''')
  ..addFlag('verbose', abbr: 'v', defaultsTo: false, negatable: false);
