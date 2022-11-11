// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:args/args.dart';
import 'package:async/async.dart';
import 'package:fxtest/fxtest.dart';
import 'package:fxutils/fxutils.dart';
import 'package:mockito/mockito.dart';
import 'package:test/test.dart';

import 'fake_fx_env.dart';
import 'helpers.dart';

class MockEnvReader extends Mock implements EnvReader {}

void main() {
  group('test name arguments are parsed correctly', () {
    test('when a dot is passed to stand in as the current directory', () {
      final fxEnv = FakeFxEnv(
        cwd: '/root/fuchsia/out/default/host_x64/gen',
      );
      var collector = TestNamesCollector(
        rawTestNames: ['.'],
        rawArgs: ['.'],
        relativeCwd: fxEnv.relativeCwd,
      );
      expect(collector.collect(), [
        [MatchableArgument.unrestricted('host_x64/gen')]
      ]);
    });
    test('when a duplicate is passed in', () {
      var collector = TestNamesCollector(
        rawTestNames: ['asdf', 'asdf', 'xyz'],
        rawArgs: ['asdf', 'asdf', 'xyz'],
        relativeCwd: FakeFxEnv.shared.relativeCwd,
      );
      expect(collector.collect(), [
        [MatchableArgument.unrestricted('asdf')],
        [MatchableArgument.unrestricted('xyz')]
      ]);
    });
    test('when a dot and duplicate are passed in', () {
      final fakeFxEnv = FakeFxEnv(cwd: '/root/fuchsia/out/default/host_x64');
      var collector = TestNamesCollector(
        rawTestNames: ['asdf', '.', 'asdf'],
        rawArgs: ['asdf', '.', 'asdf', '--some-flag'],
        relativeCwd: fakeFxEnv.relativeCwd,
      );
      expect(collector.collect(), [
        [MatchableArgument.unrestricted('asdf')],
        [MatchableArgument.unrestricted('host_x64')]
      ]);
    });

    test('when a dot is passed from the build directory', () {
      final fakeFxEnv = FakeFxEnv(cwd: '/root/fuchsia/out/default');
      var collector = TestNamesCollector(
        rawTestNames: ['.'],
        rawArgs: ['.'],
        relativeCwd: fakeFxEnv.relativeCwd,
      );
      expect(collector.collect(), [
        [MatchableArgument.unrestricted('.')]
      ]);
    });
  });

  group('default test arguments are merged correctly', () {
    test('when the option already exists', () {
      var args = TestArguments(
        parser: ArgParser()..addOption('some-option'),
        rawArgs: ['--some-option', 'RIGHT ANSWER'],
        defaultRawArgs: {'--some-option': 'WRONG ANSWER'},
      );
      expect(args.parsedArgs['some-option'], 'RIGHT ANSWER');
    });

    test('when the option does not already exist', () {
      var args = TestArguments(
        parser: ArgParser()..addOption('some-option'),
        rawArgs: [],
        defaultRawArgs: {'--some-option': 'RIGHT ANSWER'},
      );
      expect(args.parsedArgs['some-option'], 'RIGHT ANSWER');
    });

    test('when the flag already exists', () {
      var args = TestArguments(
        parser: ArgParser()
          ..addFlag('some-flag', defaultsTo: false, negatable: true),
        rawArgs: ['--no-some-flag'],
        defaultRawArgs: {'--some-flag': null},
      );
      expect(args.parsedArgs['some-flag'], false);
    });

    test('when the flag does not already exist', () {
      var args = TestArguments(
        parser: ArgParser()
          ..addFlag('some-flag', defaultsTo: false, negatable: true),
        rawArgs: [],
        defaultRawArgs: {'--some-flag': null},
      );
      expect(args.parsedArgs['some-flag'], true);
    });

    test('with abbreviations', () {
      // Bit of a null-case, this one
      var args = TestArguments(
        parser: ArgParser()
          ..addFlag('some-flag', defaultsTo: false, negatable: true, abbr: 's'),
        rawArgs: ['-s'],
        defaultRawArgs: {'--some-flag': null},
      );
      expect(args.parsedArgs['some-flag'], true);
    });

    test('with abbreviations and opposite default', () {
      // Bit of a null-case, this one
      var args = TestArguments(
        parser: ArgParser()
          ..addFlag('some-flag', defaultsTo: false, negatable: true, abbr: 's'),
        rawArgs: ['-s'],
        defaultRawArgs: {'--no-some-flag': null},
      );
      expect(args.parsedArgs['some-flag'], true);
    });

    test('with abbreviations and options', () {
      // Bit of a null-case, this one
      var args = TestArguments(
        parser: ArgParser()..addOption('some-flag', abbr: 's'),
        rawArgs: ['-s', 'RIGHT-ANSWER'],
        defaultRawArgs: {'--some-flag': 'WRONG-ANSWER'},
      );
      expect(args.parsedArgs['some-flag'], 'RIGHT-ANSWER');
    });

    test('when an abbreviation appears as a value', () {
      // Bit of a null-case, this one
      var args = TestArguments(
        parser: ArgParser()
          ..addOption('some-flag', abbr: 's')
          ..addOption('other-flag'),
        rawArgs: ['--other-flag', 's', '--some-flag', 'RIGHT-ANSWER'],
        defaultRawArgs: {'--some-flag': 'WRONG-ANSWER'},
      );
      expect(args.parsedArgs['some-flag'], 'RIGHT-ANSWER');
    });
  });

  group('arguments are passed through correctly', () {
    void _ignoreEvents(TestEvent _) {}
    TestsManifestReader tr = TestsManifestReader();
    List<TestDefinition> testDefinitions = [
      TestDefinition(
        buildDir: FakeFxEnv.shared.outputDir,
        name: 'device test v2',
        os: 'fuchsia',
        packageUrl:
            PackageUrl.fromString('fuchsia-pkg://fuchsia.com/fancy#test.cm'),
      ),
      TestDefinition(
        buildDir: FakeFxEnv.shared.outputDir,
        name: 'example-test',
        os: 'linux',
        path: '/asdf',
      ),
    ];

    test('when there are pass-thru commands for binary tests', () async {
      var testsConfig = TestsConfig.fromRawArgs(
        rawArgs: ['example-test', '--', '--xyz'],
        fxEnv: FakeFxEnv.shared,
      );
      var cmd = FuchsiaTestCommand.fromConfig(
        testsConfig,
        testRunnerBuilder: (TestsConfig testsConfig) =>
            FakeTestRunner.passing(),
      );
      ParsedManifest manifest = tr.aggregateTests(
        eventEmitter: _ignoreEvents,
        testBundleBuilder: cmd.testBundleBuilder,
        testDefinitions: testDefinitions,
        testsConfig: testsConfig,
      );
      expect(manifest.testBundles, hasLength(1));
      var stream = StreamQueue(manifest.testBundles[0].run());

      TestEvent event = await stream.next;
      expect(event, isA<TestStarted>());
      event = await stream.next;
      expect(event, isA<TestResult>());
      TestResult resultEvent = event as TestResult;

      // [FakeTestRunner] passes args through to its stdout, so we can check
      // that the args were in fact passed through by evaluating that
      expect(resultEvent.message, '--xyz');
    });

    test('when there are pass-thru commands for suite tests', () async {
      var testsConfig = TestsConfig.fromRawArgs(
        rawArgs: ['example-test', '--', '--xyz'],
        fxEnv: FakeFxEnv.shared,
      );
      var cmd = FuchsiaTestCommand.fromConfig(
        testsConfig,
        testRunnerBuilder: (TestsConfig testsConfig) =>
            FakeTestRunner.passing(),
      );
      var bundle = cmd.testBundleBuilder(testDefinitions[0]);
      var stream = StreamQueue(bundle.run());

      TestEvent event = await stream.next;
      expect(event, isA<TestStarted>());
      event = await stream.next;
      expect(event, isA<TestResult>());
      TestResult resultEvent = event as TestResult;

      // [FakeTestRunner] passes args through to its stdout, so we can check
      // that the args were in fact passed through by evaluating that
      expect(resultEvent.message,
          "ffx test run '--disable-output-directory' fuchsia-pkg://fuchsia.com/fancy#meta/test.cm -- --xyz");
    });

    test('when there are no pass-thru commands', () async {
      var testsConfig = TestsConfig.fromRawArgs(
        rawArgs: ['example-test'],
        fxEnv: FakeFxEnv.shared,
      );
      var cmd = FuchsiaTestCommand.fromConfig(
        testsConfig,
        testRunnerBuilder: (TestsConfig testsConfig) =>
            FakeTestRunner.passing(),
      );
      ParsedManifest manifest = tr.aggregateTests(
        eventEmitter: _ignoreEvents,
        testBundleBuilder: cmd.testBundleBuilder,
        testDefinitions: testDefinitions,
        testsConfig: testsConfig,
      );
      expect(manifest.testBundles, hasLength(1));
      var stream = StreamQueue(manifest.testBundles[0].run());

      TestEvent event = await stream.next;
      expect(event, isA<TestStarted>());
      event = await stream.next;
      expect(event, isA<TestResult>());
      TestResult resultEvent = event as TestResult;

      // [FakeTestRunner] passes args through to its stdout, so we can check
      // that the args were in fact passed through by evaluating that
      expect(resultEvent.message, '');
    });

    test('after parsing with "--" in middle', () {
      List<List<String>> splitArgs = TestArguments.splitArgs(
        ['asdf', 'ASDF', '--', 'some', 'flag'],
      );
      expect(splitArgs, hasLength(2));
      expect(splitArgs[0], ['asdf', 'ASDF']);
      expect(splitArgs[1], ['some', 'flag']);
    });
    test('after parsing with "--" at end', () {
      List<List<String>> splitArgs = TestArguments.splitArgs(
        ['asdf', 'ASDF', '--'],
      );
      expect(splitArgs, hasLength(2));
      expect(splitArgs[0], ['asdf', 'ASDF']);
      expect(splitArgs[1], hasLength(0));
    });

    test('after parsing with "--" at beginning', () {
      List<List<String>> splitArgs = TestArguments.splitArgs(
        ['--', 'asdf', 'ASDF'],
      );
      expect(splitArgs, hasLength(2));
      expect(splitArgs[0], hasLength(0));
      expect(splitArgs[1], ['asdf', 'ASDF']);
    });

    test('after parsing with no "--"', () {
      List<List<String>> splitArgs = TestArguments.splitArgs(
        ['asdf', 'ASDF'],
      );
      expect(splitArgs, hasLength(2));
      expect(splitArgs[0], ['asdf', 'ASDF']);
      expect(splitArgs[1], hasLength(0));
    });
  });

  group('flags are parsed correctly', () {
    test('regarding --log', () {
      // The default workflow (not using CLI wrappers) doesn't write logs
      var testsConfig = TestsConfig.fromRawArgs(
        rawArgs: [],
        fxEnv: FakeFxEnv.shared,
      );
      expect(testsConfig.flags.shouldLog, false);
    });

    test('regarding --log from real code path', () {
      var cliCmdWrapper = FuchsiaTestCommandCli(
        [],
        usage: (argParser) => {},
        fxEnv: FakeFxEnv.shared,
      );
      var cmd = cliCmdWrapper.createCommand();
      expect(cmd.testsConfig.flags.shouldLog, true);
    });

    test('with --info', () {
      var testsConfig = TestsConfig.fromRawArgs(
        rawArgs: ['--info'],
        fxEnv: FakeFxEnv.shared,
      );
      expect(testsConfig.flags.infoOnly, true);
      expect(testsConfig.flags.dryRun, true);
      expect(testsConfig.flags.shouldRebuild, false);
    });

    test('with --dry', () {
      var testsConfig = TestsConfig.fromRawArgs(
        rawArgs: ['--dry'],
        fxEnv: FakeFxEnv.shared,
      );
      expect(testsConfig.flags.infoOnly, false);
      expect(testsConfig.flags.dryRun, true);
      expect(testsConfig.flags.shouldRebuild, false);
    });

    test('with --no-build', () {
      var testsConfig = TestsConfig.fromRawArgs(
        rawArgs: ['--no-build'],
        fxEnv: FakeFxEnv.shared,
      );
      expect(testsConfig.flags.shouldRebuild, false);
    });

    test('with no --no-build', () {
      var testsConfig = TestsConfig.fromRawArgs(
        rawArgs: [''],
        fxEnv: FakeFxEnv.shared,
      );
      expect(testsConfig.flags.shouldRebuild, true);
    });

    test('--realm is deprecated', () {
      expect(
          () => TestsConfig.fromRawArgs(
                rawArgs: ['--realm=foo'],
                fxEnv: FakeFxEnv.shared,
              ),
          throwsA(isA<InvalidOption>()));
    });

    test('with no --restrict-logs', () {
      var testsConfig = TestsConfig.fromRawArgs(
        rawArgs: [],
        fxEnv: FakeFxEnv.shared,
      );
      // Still true because that is the default
      expect(testsConfig.flags.shouldRestrictLogs, true);
      expect(testsConfig.runnerTokens[TestType.suite],
          ['--disable-output-directory']);
    });

    test('with --no-restrict-logs', () {
      var testsConfig = TestsConfig.fromRawArgs(
        rawArgs: ['--no-restrict-logs'],
        fxEnv: FakeFxEnv.shared,
      );
      expect(testsConfig.flags.shouldRestrictLogs, false);
      expect(testsConfig.runnerTokens[TestType.suite],
          ['--disable-output-directory']);
    });

    test('with --restrict-logs', () {
      var testsConfig = TestsConfig.fromRawArgs(
        rawArgs: ['--restrict-logs'],
        fxEnv: FakeFxEnv.shared,
      );
      expect(testsConfig.flags.shouldRestrictLogs, true);
      expect(testsConfig.runnerTokens[TestType.suite],
          ['--disable-output-directory']);
    });

    test('with --min-severity-logs', () {
      var testsConfig = TestsConfig.fromRawArgs(
        rawArgs: ['--min-severity-logs=WARN'],
        fxEnv: FakeFxEnv.shared,
      );
      expect(testsConfig.flags.minSeverityLogs, 'WARN');
      expect(testsConfig.runnerTokens[TestType.suite],
          contains('--min-severity-logs'));
      expect(testsConfig.runnerTokens[TestType.suite], contains('WARN'));
    });

    test('with --test-filter', () {
      var testsConfig = TestsConfig.fromRawArgs(
        rawArgs: ['--test-filter=mytest'],
        fxEnv: FakeFxEnv.shared,
      );
      expect(testsConfig.flags.testFilter, ['mytest']);
      expect(testsConfig.flags.count, null);
      expect(testsConfig.flags.runDisabledTests, false);

      expect(
          testsConfig.runnerTokens[TestType.suite], contains('--test-filter'));
      expect(testsConfig.runnerTokens[TestType.suite], contains('mytest'));
      expect(
          testsConfig.runnerTokens[TestType.suite], isNot(contains('--count')));
      expect(testsConfig.runnerTokens[TestType.suite],
          isNot(contains('--also-run-disabled-tests')));
    });

    test('with multiple --test-filter', () {
      var testsConfig = TestsConfig.fromRawArgs(
        rawArgs: ['--test-filter=mypattern1', '--test-filter=mypattern2'],
        fxEnv: FakeFxEnv.shared,
      );
      expect(testsConfig.flags.testFilter, ['mypattern1', 'mypattern2']);
      expect(testsConfig.flags.count, null);
      expect(testsConfig.flags.runDisabledTests, false);

      expect(
          testsConfig.runnerTokens[TestType.suite]
              ?.where((e) => e == '--test-filter'),
          ['--test-filter', '--test-filter']);
      expect(testsConfig.runnerTokens[TestType.suite], contains('mypattern1'));
      expect(testsConfig.runnerTokens[TestType.suite], contains('mypattern2'));
      expect(
          testsConfig.runnerTokens[TestType.suite], isNot(contains('--count')));
      expect(testsConfig.runnerTokens[TestType.suite],
          isNot(contains('--also-run-disabled-tests')));
    });

    test('with --timeout', () {
      var testsConfig = TestsConfig.fromRawArgs(
        rawArgs: ['--timeout=5'],
        fxEnv: FakeFxEnv.shared,
      );
      expect(testsConfig.flags.timeout, '5');

      // shouldn't be added yet. TODO - figure out a nicer refactor here so that the flags are
      // generated just once
      expect(testsConfig.runnerTokens[TestType.suite],
          isNot(contains('--timeout')));
    });

    test('with --count', () {
      var testsConfig = TestsConfig.fromRawArgs(
        rawArgs: ['--count=22'],
        fxEnv: FakeFxEnv.shared,
      );
      expect(testsConfig.flags.count, '22');
      expect(testsConfig.runnerTokens[TestType.suite],
          isNot(contains('--test-filter')));
      expect(testsConfig.runnerTokens[TestType.suite], contains('--count'));
      expect(testsConfig.runnerTokens[TestType.suite], contains('22'));
    });

    test('with --fail', () {
      var testsConfig = TestsConfig.fromRawArgs(
        rawArgs: ['--fail'],
        fxEnv: FakeFxEnv.shared,
      );
      expect(testsConfig.flags.shouldFailFast, true);
      expect(testsConfig.runnerTokens[TestType.suite],
          contains('--stop-after-failures'));
      expect(testsConfig.runnerTokens[TestType.suite], contains('1'));
    });

    test('with --fail and --count', () {
      var testsConfig = TestsConfig.fromRawArgs(
        rawArgs: ['--fail', '--count=22'],
        fxEnv: FakeFxEnv.shared,
      );
      expect(testsConfig.flags.shouldFailFast, true);
      expect(testsConfig.flags.count, '22');
      expect(testsConfig.runnerTokens[TestType.suite],
          contains('--stop-after-failures'));
      expect(testsConfig.runnerTokens[TestType.suite], contains('1'));
      expect(testsConfig.runnerTokens[TestType.suite], contains('--count'));
      expect(testsConfig.runnerTokens[TestType.suite], contains('22'));
    });

    test('with --parallel', () {
      var testsConfig = TestsConfig.fromRawArgs(
        rawArgs: ['--parallel=10'],
        fxEnv: FakeFxEnv.shared,
      );
      expect(testsConfig.flags.parallel, '10');
      expect(testsConfig.runnerTokens[TestType.suite],
          isNot(contains('--test-filter')));
      // shouldn't be added yet. TODO - figure out a nicer refactor here so that the flags are
      // generated just once
      expect(testsConfig.runnerTokens[TestType.suite],
          isNot(contains('--parallel')));
    });

    test('with --also-run-disabled-tests', () {
      var testsConfig = TestsConfig.fromRawArgs(
        rawArgs: ['--also-run-disabled-tests'],
        fxEnv: FakeFxEnv.shared,
      );
      expect(testsConfig.flags.runDisabledTests, true);
      expect(testsConfig.runnerTokens[TestType.suite],
          isNot(contains('--test-filter')));
      expect(
          testsConfig.runnerTokens[TestType.suite], contains('--run-disabled'));
    });

    test('with --show-full-moniker-in-logs', () {
      var testsConfig = TestsConfig.fromRawArgs(
        rawArgs: ['--show-full-moniker-in-logs'],
        fxEnv: FakeFxEnv.shared,
      );

      expect(testsConfig.runnerTokens[TestType.suite],
          contains('--show-full-moniker-in-logs'));
    });

    test('with --ffx-output-directory', () {
      var testsConfig = TestsConfig.fromRawArgs(
        rawArgs: ['--ffx-output-directory', '/tmp'],
        fxEnv: FakeFxEnv.shared,
      );
      expect(testsConfig.flags.ffxOutputDirectory, '/tmp');
      expect(testsConfig.runnerTokens[TestType.suite],
          isNot(contains('--disable-output-directory')));
      expect(testsConfig.dynamicRunnerTokens[TestType.suite], hasLength(1));
      expect(testsConfig.dynamicRunnerTokens[TestType.suite]![0],
          isA<FfxOutputDirectoryToken>());
      expect(
          testsConfig.dynamicRunnerTokens[TestType.suite]![0].generateTokens(),
          ['--output-directory', '/tmp/0']);
      expect(
          testsConfig.dynamicRunnerTokens[TestType.suite]![0].generateTokens(),
          ['--output-directory', '/tmp/1']);
    });

    test('without --ffx-output-directory', () {
      var testsConfig = TestsConfig.fromRawArgs(
        rawArgs: [],
        fxEnv: FakeFxEnv.shared,
      );
      expect(testsConfig.flags.ffxOutputDirectory, isNull);
      expect(testsConfig.runnerTokens[TestType.suite],
          contains('--disable-output-directory'));
      expect(testsConfig.dynamicRunnerTokens[TestType.suite], hasLength(0));
    });
  });

  group('test names are collected correctly', () {
    test('with zero test names', () {
      var collector = TestNamesCollector(
        rawArgs: [],
        rawTestNames: [],
        relativeCwd: FakeFxEnv.shared.relativeCwd,
      );
      expect(collector.collect(), [
        [MatchableArgument.empty()],
      ]);
    });

    test('with zero test names but some flags', () {
      var collector = TestNamesCollector(
        rawArgs: ['--some-flags', '--more-flags', 'asdf'],
        rawTestNames: [],
        relativeCwd: FakeFxEnv.shared.relativeCwd,
      );
      expect(collector.collect(), [
        [MatchableArgument.empty()],
      ]);
    });

    test('with one test name', () {
      var collector = TestNamesCollector(
        rawArgs: ['test_one'],
        rawTestNames: ['test_one'],
        relativeCwd: FakeFxEnv.shared.relativeCwd,
      );
      expect(collector.collect(), [
        [MatchableArgument.unrestricted('test_one')],
      ]);
    });

    test('with one test name and flags', () {
      var collector = TestNamesCollector(
        rawArgs: ['test_one', '--exact'],
        rawTestNames: ['test_one'],
        relativeCwd: FakeFxEnv.shared.relativeCwd,
      );
      expect(collector.collect(), [
        [MatchableArgument.unrestricted('test_one')],
      ]);
    });

    test('with two test names', () {
      var collector = TestNamesCollector(
        rawArgs: ['test_one', 'test_two'],
        rawTestNames: ['test_one', 'test_two'],
        relativeCwd: FakeFxEnv.shared.relativeCwd,
      );
      expect(collector.collect(), [
        [MatchableArgument.unrestricted('test_one')],
        [MatchableArgument.unrestricted('test_two')]
      ]);
    });

    test('with two test names and flags', () {
      var collector = TestNamesCollector(
        rawArgs: ['test_one', 'test_two', '--exact'],
        rawTestNames: ['test_one', 'test_two'],
        relativeCwd: FakeFxEnv.shared.relativeCwd,
      );
      expect(collector.collect(), [
        [MatchableArgument.unrestricted('test_one')],
        [MatchableArgument.unrestricted('test_two')]
      ]);
    });

    test('with one test names and AND flags', () {
      var collector = TestNamesCollector(
        rawArgs: ['test_one', '-a', 'filter-two'],
        rawTestNames: ['test_one'],
        relativeCwd: FakeFxEnv.shared.relativeCwd,
      );
      expect(collector.collect(), [
        [
          MatchableArgument.unrestricted('test_one'),
          MatchableArgument.unrestricted('filter-two')
        ],
      ]);
    });

    test('with two test names and AND flags', () {
      var collector = TestNamesCollector(
        rawArgs: ['test_one', '-a', 'filter-two', 'test_two'],
        rawTestNames: ['test_one', 'test_two'],
        relativeCwd: FakeFxEnv.shared.relativeCwd,
      );
      expect(collector.collect(), [
        [
          MatchableArgument.unrestricted('test_one'),
          MatchableArgument.unrestricted('filter-two')
        ],
        [MatchableArgument.unrestricted('test_two')],
      ]);
    });

    test('with two test names and multiple AND flags', () {
      var collector = TestNamesCollector(
        rawArgs: [
          'test_one',
          '-a',
          'filter-two',
          '-a',
          'filter-three',
          'test_two'
        ],
        rawTestNames: ['test_one', 'test_two'],
        relativeCwd: FakeFxEnv.shared.relativeCwd,
      );
      expect(collector.collect(), [
        [
          MatchableArgument.unrestricted('test_one'),
          MatchableArgument.unrestricted('filter-two'),
          MatchableArgument.unrestricted('filter-three')
        ],
        [MatchableArgument.unrestricted('test_two')],
      ]);
    });

    test('with two test names and AND flags for multiple tests', () {
      var collector = TestNamesCollector(
        rawArgs: [
          'test_one',
          '-a',
          'filter-two',
          '-a',
          'filter-three',
          'test_two',
          '-a',
          'filter-four'
        ],
        rawTestNames: ['test_one', 'test_two'],
        relativeCwd: FakeFxEnv.shared.relativeCwd,
      );
      expect(collector.collect(), [
        [
          MatchableArgument.unrestricted('test_one'),
          MatchableArgument.unrestricted('filter-two'),
          MatchableArgument.unrestricted('filter-three')
        ],
        [
          MatchableArgument.unrestricted('test_two'),
          MatchableArgument.unrestricted('filter-four')
        ],
      ]);
    });

    test('with -p & -c flags', () {
      var collector = TestNamesCollector(
        rawArgs: ['-p', 'test_one', '-c', 'filter-two'],
        rawTestNames: [],
        relativeCwd: FakeFxEnv.shared.relativeCwd,
      );
      expect(collector.collect(), [
        [
          MatchableArgument.packageName('test_one'),
          MatchableArgument.componentName('filter-two')
        ],
      ]);
    });

    test('with -c flags and test names', () {
      var collector = TestNamesCollector(
        rawArgs: ['-c', 'filter-one', 'test-two'],
        rawTestNames: ['test-two'],
        relativeCwd: FakeFxEnv.shared.relativeCwd,
      );
      expect(collector.collect(), [
        [MatchableArgument.componentName('filter-one')],
        [MatchableArgument.unrestricted('test-two')],
      ]);
    });

    test('with test names and -p & -c flags', () {
      var collector = TestNamesCollector(
        rawArgs: ['test-one', 'test-two', '-p', 'pkg-name', '-c', 'filter-two'],
        rawTestNames: ['test-one', 'test-two'],
        relativeCwd: FakeFxEnv.shared.relativeCwd,
      );
      expect(collector.collect(), [
        [
          MatchableArgument.unrestricted('test-one'),
        ],
        [
          MatchableArgument.unrestricted('test-two'),
        ],
        [
          MatchableArgument.packageName('pkg-name'),
          MatchableArgument.componentName('filter-two')
        ],
      ]);
    });

    test('with -p & -c flags and test names', () {
      var collector = TestNamesCollector(
        rawArgs: ['-p', 'pkg-name', '-c', 'filter-two', 'test-one', 'test-two'],
        rawTestNames: ['test-one', 'test-two'],
        relativeCwd: FakeFxEnv.shared.relativeCwd,
      );
      expect(collector.collect(), [
        [
          MatchableArgument.packageName('pkg-name'),
          MatchableArgument.componentName('filter-two')
        ],
        [
          MatchableArgument.unrestricted('test-one'),
        ],
        [
          MatchableArgument.unrestricted('test-two'),
        ],
      ]);
    });

    test('with test names with AND flags and -p & -c flags and test names', () {
      var collector = TestNamesCollector(
        rawArgs: [
          'test-zero',
          '-a',
          'filter-zero',
          '-p',
          'pkg-name',
          '-c',
          'filter-two',
          'test-one',
          'test-two'
        ],
        rawTestNames: ['test-zero', 'test-one', 'test-two'],
        relativeCwd: FakeFxEnv.shared.relativeCwd,
      );
      expect(collector.collect(), [
        [
          MatchableArgument.unrestricted('test-zero'),
          MatchableArgument.unrestricted('filter-zero')
        ],
        [
          MatchableArgument.packageName('pkg-name'),
          MatchableArgument.componentName('filter-two')
        ],
        [
          MatchableArgument.unrestricted('test-one'),
        ],
        [
          MatchableArgument.unrestricted('test-two'),
        ],
      ]);
    });
  });
}
