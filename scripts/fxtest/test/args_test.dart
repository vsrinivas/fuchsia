// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:async/async.dart';
import 'package:test/test.dart';
import 'package:fxtest/fxtest.dart';
import 'package:mockito/mockito.dart';
import 'helpers.dart';

class MockEnvReader extends Mock implements EnvReader {}

void main() {
  group('test name arguments are parsed correctly', () {
    test('when a dot is passed to stand in as the current directory', () {
      var envReader = MockEnvReader();
      when(envReader.getEnv('FUCHSIA_BUILD_DIR'))
          .thenReturn('/root/path/fuchsia/out/default');
      when(envReader.getCwd())
          .thenReturn('/root/path/fuchsia/out/default/host_x64/gen');
      var fuchsiaLocator = FuchsiaLocator(envReader: envReader);
      var collector = TestNamesCollector(
        rawTestNames: ['.'],
        rawArgs: ['.'],
        fuchsiaLocator: fuchsiaLocator,
      );
      expect(collector.collect(), [
        [MatchableArgument.unrestricted('host_x64/gen')]
      ]);
    });
    test('when a duplicate is passed in', () {
      var collector = TestNamesCollector(
        rawTestNames: ['asdf', 'asdf', 'xyz'],
        rawArgs: ['asdf', 'asdf', 'xyz'],
      );
      expect(collector.collect(), [
        [MatchableArgument.unrestricted('asdf')],
        [MatchableArgument.unrestricted('xyz')]
      ]);
    });
    test('when a dot and duplicate are passed in', () {
      var envReader = MockEnvReader();
      when(envReader.getEnv('FUCHSIA_BUILD_DIR'))
          .thenReturn('/root/path/fuchsia/out/default');
      when(envReader.getCwd())
          .thenReturn('/root/path/fuchsia/out/default/host_x64');
      var fuchsiaLocator = FuchsiaLocator(envReader: envReader);
      var collector = TestNamesCollector(
        rawTestNames: ['asdf', '.', 'asdf'],
        rawArgs: ['asdf', '.', 'asdf', '--some-flag'],
        fuchsiaLocator: fuchsiaLocator,
      );
      expect(collector.collect(), [
        [MatchableArgument.unrestricted('asdf')],
        [MatchableArgument.unrestricted('host_x64')]
      ]);
    });

    test('when a dot is passed from the build directory', () {
      var envReader = MockEnvReader();
      when(envReader.getEnv('FUCHSIA_BUILD_DIR'))
          .thenReturn('/root/path/fuchsia/out/default');
      when(envReader.getCwd()).thenReturn('/root/path/fuchsia/out/default');
      var fuchsiaLocator = FuchsiaLocator(envReader: envReader);
      var collector = TestNamesCollector(
        rawTestNames: ['.'],
        rawArgs: ['.'],
        fuchsiaLocator: fuchsiaLocator,
      );
      expect(collector.collect(), [
        [MatchableArgument.unrestricted('.')]
      ]);
    });
  });

  group('arguments are passed through correctly', () {
    var envReader = MockEnvReader();
    when(envReader.getEnv('FUCHSIA_DIR')).thenReturn('/root/path/fuchsia');
    when(envReader.getEnv('FUCHSIA_BUILD_DIR'))
        .thenReturn('/root/path/fuchsia/out/default');
    var fuchsiaLocator = FuchsiaLocator(envReader: envReader);

    void _ignoreEvents(TestEvent _) {}
    TestsManifestReader tr = TestsManifestReader();
    List<TestDefinition> testDefinitions = [
      TestDefinition(
        buildDir: fuchsiaLocator.buildDir,
        fx: fuchsiaLocator.fx,
        name: 'device test',
        os: 'fuchsia',
        packageUrl: 'fuchsia-pkg://fuchsia.com/fancy#test.cmx',
      ),
      TestDefinition(
        buildDir: fuchsiaLocator.buildDir,
        fx: fuchsiaLocator.fx,
        name: 'example-test',
        os: 'linux',
        path: '/asdf',
      ),
    ];

    test('when there are pass-thru commands', () async {
      var testsConfig = TestsConfig.fromRawArgs(
        rawArgs: ['example-test', '--', '--xyz'],
        fuchsiaLocator: fuchsiaLocator,
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
      TestResult resultEvent = event;

      // [FakeTestRunner] passes args through to its stdout, so we can check
      // that the args were in fact passed through by evaluating that
      expect(resultEvent.message, '--xyz');
    });

    test('when there are no pass-thru commands', () async {
      var testsConfig = TestsConfig.fromRawArgs(
        rawArgs: ['example-test'],
        fuchsiaLocator: fuchsiaLocator,
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
      TestResult resultEvent = event;

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
    test('with --info', () {
      var testsConfig = TestsConfig.fromRawArgs(rawArgs: ['--info']);
      expect(testsConfig.flags.infoOnly, true);
      expect(testsConfig.flags.dryRun, true);
      expect(testsConfig.flags.shouldRebuild, false);
    });

    test('with --dry', () {
      var testsConfig = TestsConfig.fromRawArgs(rawArgs: ['--dry']);
      expect(testsConfig.flags.infoOnly, false);
      expect(testsConfig.flags.dryRun, true);
      expect(testsConfig.flags.shouldRebuild, false);
    });

    test('with --no-build', () {
      var testsConfig = TestsConfig.fromRawArgs(rawArgs: ['--no-build']);
      expect(testsConfig.flags.shouldRebuild, false);
    });

    test('with no --no-build', () {
      var testsConfig = TestsConfig.fromRawArgs(rawArgs: ['']);
      expect(testsConfig.flags.shouldRebuild, true);
    });

    test('with --realm', () {
      var testsConfig = TestsConfig.fromRawArgs(rawArgs: ['--realm=foo']);
      expect(testsConfig.flags.realm, 'foo');
    });
  });

  group('test names are collected correctly', () {
    test('with zero test names', () {
      var collector = TestNamesCollector(
        rawArgs: [],
        rawTestNames: [],
      );
      expect(collector.collect(), [
        [MatchableArgument.empty()],
      ]);
    });

    test('with zero test names but some flags', () {
      var collector = TestNamesCollector(
        rawArgs: ['--some-flags', '--more-flags', 'asdf'],
        rawTestNames: [],
      );
      expect(collector.collect(), [
        [MatchableArgument.empty()],
      ]);
    });

    test('with one test name', () {
      var collector = TestNamesCollector(
        rawArgs: ['test_one'],
        rawTestNames: ['test_one'],
      );
      expect(collector.collect(), [
        [MatchableArgument.unrestricted('test_one')],
      ]);
    });

    test('with one test name and flags', () {
      var collector = TestNamesCollector(
        rawArgs: ['test_one', '--exact'],
        rawTestNames: ['test_one'],
      );
      expect(collector.collect(), [
        [MatchableArgument.unrestricted('test_one')],
      ]);
    });

    test('with two test names', () {
      var collector = TestNamesCollector(
        rawArgs: ['test_one', 'test_two'],
        rawTestNames: ['test_one', 'test_two'],
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
