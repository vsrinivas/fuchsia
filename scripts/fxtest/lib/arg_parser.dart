import 'package:args/args.dart';
import 'package:fxtest/fxtest.dart';

/// Test-friendly wrapper so unit tests can use `fxTestArgParser` without
/// having its environment-aware print statements blow up.
String getFriendlyBuildDir() {
  var buildDir = '//out/default';
  try {
    buildDir = FuchsiaLocator.shared.userFriendlyBuildDir;

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
  ..addFlag('updateifinbase',
      defaultsTo: true,
      negatable: true,
      help:
          'If true, invokes `fx update-if-in-base` before running device tests')
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
randomized\none.''')
  ..addFlag('exact',
      defaultsTo: false,
      help: 'If true, does not perform any fuzzy-matching on tests')
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
