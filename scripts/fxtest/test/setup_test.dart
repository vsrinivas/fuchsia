import 'package:fxtest/fxtest.dart';
import 'package:meta/meta.dart';
import 'package:mockito/mockito.dart';
import 'package:test/test.dart';
import 'helpers.dart';

class MockEnvReader extends Mock implements EnvReader {}

class FuchsiaTestCommandCliFake extends FuchsiaTestCommandCli {
  FuchsiaTestCommandCliFake()
      : super(
          ['--no-build'],
          usage: (parser) => null,
        );

  @override
  FuchsiaTestCommand buildCommand(TestsConfig testsConfig) {
    return FuchsiaTestCommandFake(
      testsConfig: testsConfig,
      outputFormatter: StdOutClosingFormatter(),
    );
  }
}

class FuchsiaTestCommandFake extends FuchsiaTestCommand {
  FuchsiaTestCommandFake({
    OutputFormatter outputFormatter,
    TestsConfig testsConfig,
  }) : super(
          analyticsReporter: AnalyticsFaker(),
          fuchsiaLocator: FuchsiaLocator.shared,
          outputFormatter: outputFormatter,
          testsConfig: testsConfig,
          testRunnerBuilder: (testsConfig) => TestRunner(),
        );
  @override
  Future<void> runTestSuite(TestsManifestReader manifestReader) async {
    emitEvent(BeginningTests());
  }
}

class StdOutClosingFormatter extends OutputFormatter {
  @override
  void update(TestEvent event) {
    forcefullyClose();
  }
}

class AnalyticsFaker extends AnalyticsReporter {
  List<List<String>> reportHistory = [];

  @override
  Future<void> report({
    @required String subcommand,
    @required String action,
    String label,
  }) async {
    reportHistory.add([subcommand, action, label]);
  }
}

void main() {
  group('fuchsia directories are located correctly', () {
    test('when the build directory is inside the checkout', () {
      var envReader = MockEnvReader();
      when(envReader.getEnv('FUCHSIA_DIR')).thenReturn('/root/path/fuchsia');
      when(envReader.getEnv('FUCHSIA_BUILD_DIR'))
          .thenReturn('/root/path/fuchsia/out/default');
      var fuchsiaLocator = FuchsiaLocator(envReader: envReader);
      expect(fuchsiaLocator.fuchsiaDir, '/root/path/fuchsia');
      expect(fuchsiaLocator.buildDir, '/root/path/fuchsia/out/default');
      expect(fuchsiaLocator.relativeBuildDir, '/out/default');
      expect(fuchsiaLocator.userFriendlyBuildDir, '//out/default');
    });

    test('when the cwd is requested from the build dir itself', () {
      var envReader = MockEnvReader();
      when(envReader.getEnv('FUCHSIA_DIR')).thenReturn('/root/path/fuchsia');
      when(envReader.getEnv('FUCHSIA_BUILD_DIR'))
          .thenReturn('/root/path/fuchsia/out/default');
      when(envReader.getCwd()).thenReturn('/root/path/fuchsia/out/default');
      var fuchsiaLocator = FuchsiaLocator(envReader: envReader);
      expect(fuchsiaLocator.relativeCwd, '.');
    });

    test('when the cwd is requested from within the build dir', () {
      var envReader = MockEnvReader();
      when(envReader.getEnv('FUCHSIA_DIR')).thenReturn('/root/path/fuchsia');
      when(envReader.getEnv('FUCHSIA_BUILD_DIR'))
          .thenReturn('/root/path/fuchsia/out/default');
      when(envReader.getCwd())
          .thenReturn('/root/path/fuchsia/out/default/host_x64');
      var fuchsiaLocator = FuchsiaLocator(envReader: envReader);
      expect(fuchsiaLocator.relativeCwd, 'host_x64');
    });

    test(
        'when the cwd is requested from within the tree but not within '
        'the build dir', () {
      var envReader = MockEnvReader();
      when(envReader.getEnv('FUCHSIA_DIR')).thenReturn('/root/path/fuchsia');
      when(envReader.getEnv('FUCHSIA_BUILD_DIR'))
          .thenReturn('/root/path/fuchsia/out/default');
      when(envReader.getCwd()).thenReturn('/root/path/fuchsia/tools');
      var fuchsiaLocator = FuchsiaLocator(envReader: envReader);
      expect(fuchsiaLocator.relativeCwd, '/root/path/fuchsia/tools');
    });

    test('when fuchsia tree directory path does not contain "fuchsia"', () {
      var envReader = MockEnvReader();
      when(envReader.getEnv('FUCHSIA_DIR')).thenReturn('/root/path/dev');
      when(envReader.getEnv('FUCHSIA_BUILD_DIR'))
          .thenReturn('/root/path/dev/out/default');
      when(envReader.getCwd()).thenReturn('/root/path/dev/out/default');
      var fuchsiaLocator = FuchsiaLocator(envReader: envReader);
      expect(fuchsiaLocator.relativeCwd, '.');
    });
  });

  group('prechecks', () {
    test('raise errors if fx is missing', () {
      var logged = false;
      var cmdCli = FuchsiaTestCommandCli([''], usage: (parser) {});
      expect(
        () => cmdCli.preRunChecks(
          '/fake/fx',
          (Object obj) => logged = true,
        ),
        throwsA(TypeMatcher<MissingFxException>()),
      );
      expect(logged, false);
    });

    test('prints tests when asked', () async {
      var logged = false;
      var calledUsage = false;
      var cmdCli = FuchsiaTestCommandCli(['--printtests'], usage: (parser) {
        calledUsage = true;
      });
      bool shouldRun = await cmdCli.preRunChecks(
        '/fake/fx',
        (Object obj) => logged = true,
      );
      expect(calledUsage, false);
      expect(shouldRun, false);
      // Passing `--printtests` does its thing and exits before `/fake/fx` is
      // validated, which would otherwise throw an exception
      expect(logged, true);
    });
  });

  group('command cli-wrapper', () {
    test('throws OutputClosedException exception stdout is closed', () async {
      var cmdCli = FuchsiaTestCommandCliFake();
      expect(
        cmdCli.run(),
        throwsA(TypeMatcher<OutputClosedException>()),
      );
    });
  });

  group('test analytics are reporting', () {
    var envReader = MockEnvReader();
    when(envReader.getEnv('FUCHSIA_DIR')).thenReturn('/root/path/fuchsia');
    var fuchsiaLocator = FuchsiaLocator(envReader: envReader);
    var testsConfig = TestsConfig.fromRawArgs(rawArgs: []);
    test('functions on real test runs', () async {
      var cmd = FuchsiaTestCommand(
        analyticsReporter: AnalyticsFaker(),
        fuchsiaLocator: fuchsiaLocator,
        outputFormatter: OutputFormatter.fromConfig(
          testsConfig,
          buffer: OutputBuffer.locMemIO(),
        ),
        testRunnerBuilder: (testsConfig) => FakeTestRunner.passing(),
        testsConfig: TestsConfig.fromRawArgs(rawArgs: []),
      );
      var testBundles = <TestBundle>[
        cmd.testBundleBuilder(
          TestDefinition(
            buildDir: '/',
            command: ['asdf'],
            fx: fuchsiaLocator.fx,
            name: 'Big Test',
            os: 'linux',
          ),
        ),
      ];
      await cmd.runTests(testBundles);
      await cmd.cleanUp();
      expect(
        // ignore: avoid_as
        (cmd.analyticsReporter as AnalyticsFaker).reportHistory,
        [
          ['test', 'number', '1']
        ],
      );
    });
    test('is silent on dry runs', () async {
      var cmd = FuchsiaTestCommand(
        analyticsReporter: AnalyticsFaker(),
        fuchsiaLocator: fuchsiaLocator,
        outputFormatter: OutputFormatter.fromConfig(
          testsConfig,
          buffer: OutputBuffer.locMemIO(),
        ),
        testRunnerBuilder: (testsConfig) => FakeTestRunner.passing(),
        testsConfig: TestsConfig.fromRawArgs(rawArgs: ['--dry']),
      );
      var testBundles = <TestBundle>[
        cmd.testBundleBuilder(
          TestDefinition(
            buildDir: '/',
            command: ['asdf'],
            fx: fuchsiaLocator.fx,
            name: 'Big Test',
            os: 'linux',
          ),
        ),
      ];
      await cmd.runTests(testBundles);
      await cmd.cleanUp();
      expect(
        // ignore: avoid_as
        (cmd.analyticsReporter as AnalyticsFaker).reportHistory,
        hasLength(0),
      );
    });
  });
}
