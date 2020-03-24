import 'dart:io';
import 'package:args/args.dart';
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
        [
          ['.'],
        ],
        fuchsiaLocator: fuchsiaLocator,
      );
      expect(collector.collect(), ['host_x64/gen']);
    });
    test('when a duplicate is passed in', () {
      var collector = TestNamesCollector([
        ['asdf'],
        ['asdf', 'xyz']
      ]);
      expect(collector.collect(), ['asdf', 'xyz']);
    });
    test('when a dot and duplicate are passed in', () {
      var envReader = MockEnvReader();
      when(envReader.getEnv('FUCHSIA_BUILD_DIR'))
          .thenReturn('/root/path/fuchsia/out/default');
      when(envReader.getCwd())
          .thenReturn('/root/path/fuchsia/out/default/host_x64');
      var fuchsiaLocator = FuchsiaLocator(envReader: envReader);
      var collector = TestNamesCollector(
        [
          ['asdf'],
          ['asdf', '.']
        ],
        fuchsiaLocator: fuchsiaLocator,
      );
      expect(collector.collect(), ['asdf', 'host_x64']);
    });

    test('when a dot is passed from the build directory', () {
      var envReader = MockEnvReader();
      when(envReader.getEnv('FUCHSIA_BUILD_DIR'))
          .thenReturn('/root/path/fuchsia/out/default');
      when(envReader.getCwd()).thenReturn('/root/path/fuchsia/out/default');
      var fuchsiaLocator = FuchsiaLocator(envReader: envReader);
      var collector = TestNamesCollector(
        [
          ['.']
        ],
        fuchsiaLocator: fuchsiaLocator,
      );
      expect(collector.collect(), ['.']);
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
        name: 'example test',
        os: 'linux',
        path: '/asdf',
      ),
    ];

    test('when there are pass-thru commands', () async {
      var testsConfig = TestsConfig(
        flags: Flags(),
        runnerTokens: const [],
        testNames: ['example test'],
        passThroughTokens: ['--xyz'],
      );
      ParsedManifest manifest = tr.aggregateTests(
        buildDir: '/custom',
        eventEmitter: _ignoreEvents,
        testDefinitions: testDefinitions,
        testsConfig: testsConfig,
        testRunner: FakeTestRunner.passing(),
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
      var testsConfig = TestsConfig(
        flags: Flags(),
        passThroughTokens: [''],
        runnerTokens: const [],
        testNames: ['example test'],
      );
      ParsedManifest manifest = tr.aggregateTests(
        buildDir: '/custom',
        eventEmitter: _ignoreEvents,
        testDefinitions: testDefinitions,
        testsConfig: testsConfig,
        testRunner: FakeTestRunner.passing(),
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
      List<List<String>> splitArgs = FuchsiaTestCommandCli.splitArgs(
        ['asdf', 'ASDF', '--', 'some', 'flag'],
      );
      expect(splitArgs, hasLength(2));
      expect(splitArgs[0], ['asdf', 'ASDF']);
      expect(splitArgs[1], ['some', 'flag']);
    });
    test('after parsing with "--" at end', () {
      List<List<String>> splitArgs = FuchsiaTestCommandCli.splitArgs(
        ['asdf', 'ASDF', '--'],
      );
      expect(splitArgs, hasLength(2));
      expect(splitArgs[0], ['asdf', 'ASDF']);
      expect(splitArgs[1], hasLength(0));
    });

    test('after parsing with "--" at beginning', () {
      List<List<String>> splitArgs = FuchsiaTestCommandCli.splitArgs(
        ['--', 'asdf', 'ASDF'],
      );
      expect(splitArgs, hasLength(2));
      expect(splitArgs[0], hasLength(0));
      expect(splitArgs[1], ['asdf', 'ASDF']);
    });

    test('after parsing with no "--"', () {
      List<List<String>> splitArgs = FuchsiaTestCommandCli.splitArgs(
        ['asdf', 'ASDF'],
      );
      expect(splitArgs, hasLength(2));
      expect(splitArgs[0], ['asdf', 'ASDF']);
      expect(splitArgs[1], hasLength(0));
    });
  });

  group('flags are parsed correctly', () {
    test('with --info', () {
      ArgResults results = fxTestArgParser.parse(['--info']);
      var testsConfig = TestsConfig.fromArgResults(results: results);
      expect(testsConfig.flags.infoOnly, true);
      expect(testsConfig.flags.dryRun, true);
      expect(testsConfig.flags.shouldRebuild, false);
    });

    test('with --dry', () {
      ArgResults results = fxTestArgParser.parse(['--dry']);
      var testsConfig = TestsConfig.fromArgResults(results: results);
      expect(testsConfig.flags.infoOnly, false);
      expect(testsConfig.flags.dryRun, true);
      expect(testsConfig.flags.shouldRebuild, false);
    });

    test('with --no-build', () {
      ArgResults results = fxTestArgParser.parse(['--no-build']);
      var testsConfig = TestsConfig.fromArgResults(results: results);
      expect(testsConfig.flags.shouldRebuild, false);
    });

    test('with no --no-build', () {
      ArgResults results = fxTestArgParser.parse(['']);
      var testsConfig = TestsConfig.fromArgResults(results: results);
      expect(testsConfig.flags.shouldRebuild, true);
    });

    test('with --realm', () {
      ArgResults results = fxTestArgParser.parse(['--realm=foo']);
      var testsConfig = TestsConfig.fromArgResults(results: results);
      expect(testsConfig.flags.realm, 'foo');
    });
  });
}
