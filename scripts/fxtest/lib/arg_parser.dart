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
          '`${getFriendlyBuildDir()}/tests.json`')
  ..addFlag('build',
      defaultsTo: true,
      negatable: true,
      help: 'If true, invokes `fx build` before running the test suite')
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
