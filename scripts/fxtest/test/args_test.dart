import 'dart:io';
import 'package:async/async.dart';
import 'package:test/test.dart';
import 'package:fxtest/fxtest.dart';
import 'package:meta/meta.dart';
import 'package:mockito/mockito.dart';

class MockEnvReader extends Mock implements EnvReader {}

// Mock this because it creates processes
class FakeTestRunner extends Fake implements TestRunner {
  final int exitCode;

  FakeTestRunner(this.exitCode);
  FakeTestRunner.passing() : exitCode = 0;
  FakeTestRunner.failing() : exitCode = 2;

  @override
  Future<ProcessResult> run(
    String command,
    List<String> args, {
    @required String fx,
    @required String workingDirectory,
    Function(String) realtimeOutputSink,
    Function(String) realtimeErrorSink,
  }) async {
    String _stdout = args.join(' ');
    if (realtimeOutputSink != null) {
      realtimeOutputSink(_stdout);
    }
    String _stderr = workingDirectory.toString();
    if (realtimeErrorSink != null) {
      realtimeErrorSink(_stderr);
    }
    return Future.value(
      ProcessResult(1, exitCode, _stdout, _stderr),
    );
  }
}

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
        ['host_x64/gen']
      ]);
    });
    test('when a duplicate is passed in', () {
      var collector = TestNamesCollector(
        rawTestNames: ['asdf', 'asdf', 'xyz'],
        rawArgs: ['asdf', 'asdf', 'xyz'],
      );
      expect(collector.collect(), [
        ['asdf'],
        ['xyz']
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
        ['asdf'],
        ['host_x64']
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
        ['.']
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
        testRunner: FakeTestRunner.passing(),
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
        testRunner: FakeTestRunner.passing(),
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
    test('with one test name', () {
      var collector = TestNamesCollector(
        rawArgs: ['test_one'],
        rawTestNames: ['test_one'],
      );
      expect(collector.collect(), [
        ['test_one'],
      ]);
    });

    test('with one test name and flags', () {
      var collector = TestNamesCollector(
        rawArgs: ['test_one', '--exact'],
        rawTestNames: ['test_one'],
      );
      expect(collector.collect(), [
        ['test_one'],
      ]);
    });

    test('with two test names', () {
      var collector = TestNamesCollector(
        rawArgs: ['test_one', 'test_two'],
        rawTestNames: ['test_one', 'test_two'],
      );
      expect(collector.collect(), [
        ['test_one'],
        ['test_two']
      ]);
    });

    test('with two test names and flags', () {
      var collector = TestNamesCollector(
        rawArgs: ['test_one', 'test_two', '--exact'],
        rawTestNames: ['test_one', 'test_two'],
      );
      expect(collector.collect(), [
        ['test_one'],
        ['test_two']
      ]);
    });

    test('with one test names and AND flags', () {
      var collector = TestNamesCollector(
        rawArgs: ['test_one', '-a', 'filter-two'],
        rawTestNames: ['test_one'],
      );
      expect(collector.collect(), [
        ['test_one', 'filter-two'],
      ]);
    });
    test('with two test names and AND flags', () {
      var collector = TestNamesCollector(
        rawArgs: ['test_one', '-a', 'filter-two', 'test_two'],
        rawTestNames: ['test_one', 'test_two'],
      );
      expect(collector.collect(), [
        ['test_one', 'filter-two'],
        ['test_two'],
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
        ['test_one', 'filter-two', 'filter-three'],
        ['test_two'],
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
        ['test_one', 'filter-two', 'filter-three'],
        ['test_two', 'filter-four'],
      ]);
    });
  });
}
