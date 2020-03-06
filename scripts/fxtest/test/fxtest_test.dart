import 'dart:io';
import 'package:args/args.dart';
import 'package:async/async.dart';
import 'package:fxtest/fxtest.dart';
import 'package:meta/meta.dart';
import 'package:mockito/mockito.dart';
import 'package:test/test.dart';

// Mock this because it checks environment variables
class MockEnvReader extends Mock implements EnvReader {}

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
  group('tests.json entries are correctly parsed', () {
    var envReader = MockEnvReader();
    when(envReader.getEnv('FUCHSIA_DIR')).thenReturn('/root/path/fuchsia');
    when(envReader.getEnv('FUCHSIA_BUILD_DIR')).thenReturn(
      '/root/path/fuchsia/out/default',
    );
    var fuchsiaLocator = FuchsiaLocator(envReader: envReader);
    test('with respect to custom fuchsia locations', () {
      var testDef = TestDefinition(
        buildDir: fuchsiaLocator.buildDir,
        fx: fuchsiaLocator.fx, // <-- this one is all that matters for this test
        os: 'linux',
        path: 'random-letters',
        name: 'host test',
      );
      expect(
        testDef.executionHandle.fx,
        '/root/path/fuchsia/.jiri_root/bin/fx',
      );
    });

    test('for host tests', () {
      TestsManifestReader tr = TestsManifestReader();
      List<dynamic> testJson = [
        {
          'environments': [],
          'test': {
            'cpu': 'x64',
            'deps_file':
                'host_x64/gen/topaz/tools/doc_checker/doc_checker_tests.deps.json',
            'path': 'host_x64/doc_checker_tests',
            'name': '//topaz/tools/doc_checker:doc_checker_tests',
            'os': 'linux'
          }
        },
      ];
      List<TestDefinition> tds = tr.parseManifest(
        testJson: testJson,
        buildDir: fuchsiaLocator.buildDir,
        fxLocation: fuchsiaLocator.fx,
      );
      expect(tds, hasLength(1));
      expect(tds[0].packageUrl, '');
      expect(tds[0].depsFile, testJson[0]['test']['deps_file']);
      expect(tds[0].path, testJson[0]['test']['path']);
      expect(tds[0].name, testJson[0]['test']['name']);
      expect(tds[0].cpu, testJson[0]['test']['cpu']);
      expect(tds[0].os, testJson[0]['test']['os']);
      expect(tds[0].executionHandle.testType, TestType.host);
    });
    test('for device tests', () {
      TestsManifestReader tr = TestsManifestReader();
      List<dynamic> testJson = [
        {
          'environments': [],
          'test': {
            'cpu': 'arm64',
            'path':
                '/pkgfs/packages/run_test_component_test/0/test/run_test_component_test',
            'name':
                '//garnet/bin/run_test_component/test:run_test_component_test',
            'os': 'fuchsia',
            'package_url':
                'fuchsia-pkg://fuchsia.com/run_test_component_test#meta/run_test_component_test.cmx',
          }
        },
      ];
      List<TestDefinition> tds = tr.parseManifest(
        testJson: testJson,
        buildDir: fuchsiaLocator.buildDir,
        fxLocation: fuchsiaLocator.fx,
      );
      expect(tds, hasLength(1));
      expect(tds[0].packageUrl, testJson[0]['test']['package_url']);
      expect(tds[0].depsFile, '');
      expect(tds[0].path, testJson[0]['test']['path']);
      expect(tds[0].name, testJson[0]['test']['name']);
      expect(tds[0].cpu, testJson[0]['test']['cpu']);
      expect(tds[0].os, testJson[0]['test']['os']);
      expect(tds[0].executionHandle.testType, TestType.component);
    });
    test('for unsupported tests', () {
      TestsManifestReader tr = TestsManifestReader();
      List<dynamic> testJson = [
        {
          'environments': [],
          'test': {
            'cpu': 'arm64',
            'name':
                '//garnet/bin/run_test_component/test:run_test_component_test',
            'os': 'fuchsia',
          }
        },
      ];
      List<TestDefinition> tds = tr.parseManifest(
        testJson: testJson,
        buildDir: fuchsiaLocator.buildDir,
        fxLocation: fuchsiaLocator.fx,
      );
      expect(tds, hasLength(1));
      expect(tds[0].path, '');
      expect(tds[0].executionHandle.testType, TestType.unsupported);
    });

    test('for unsupported device tests', () {
      TestsManifestReader tr = TestsManifestReader();
      List<dynamic> testJson = [
        {
          'environments': [],
          'test': {
            'cpu': 'arm64',
            'name': 'some_name',
            'path': '//asdf',
            'os': 'fuchsia',
          }
        },
      ];
      List<TestDefinition> tds = tr.parseManifest(
        testJson: testJson,
        buildDir: fuchsiaLocator.buildDir,
        fxLocation: fuchsiaLocator.fx,
      );
      expect(tds, hasLength(1));
      expect(tds[0].executionHandle.testType, TestType.unsupportedDeviceTest);
    });
  });

  group('tests are aggregated correctly', () {
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
        os: 'fuchsia',
        fx: fuchsiaLocator.fx,
        packageUrl: 'fuchsia-pkg://fuchsia.com/fancy#superBigTest.cmx',
        name: 'device test',
      ),
      TestDefinition(
        buildDir: fuchsiaLocator.buildDir,
        fx: fuchsiaLocator.fx,
        os: 'linux',
        path: '/asdf',
        name: '//host/test',
      ),
    ];

    var testRunner = TestRunner();

    // Helper function to parse lots of data for tests
    ParsedManifest parseFromArgs({
      List<String> args = const [],
      List<TestDefinition> testDefs,
    }) {
      TestsConfig testsConfig;
      var parsedArgs = fxTestArgParser.parse(args);
      var testNamesCollector = TestNamesCollector([
        parsedArgs['testNames'],
        parsedArgs.rest,
      ]);
      testsConfig = TestsConfig.fromArgResults(
        results: parsedArgs,
        testNames: testNamesCollector.collect(),
      );
      return tr.aggregateTests(
        buildDir: fuchsiaLocator.buildDir,
        eventEmitter: _ignoreEvents,
        exactMatching: testsConfig.flags.exactMatches,
        testsConfig: testsConfig,
        testDefinitions: testDefs ?? testDefinitions,
        testRunner: testRunner,
      );
    }

    test('when the --exact flag is passed for a test name', () {
      // --exact correctly catches exact name matches
      ParsedManifest parsedManifest =
          parseFromArgs(args: ['//host/test', '--exact']);
      expect(parsedManifest.testBundles, hasLength(1));
      expect(parsedManifest.testBundles[0].testDefinition.name, '//host/test');

      // --exact kills partial name matches
      parsedManifest = parseFromArgs(args: ['//host', '--exact']);
      expect(parsedManifest.testBundles, hasLength(0));
    });

    test('when the --exact flag is passed for a test path', () {
      // --exact correctly catches exact path matches
      ParsedManifest parsedManifest = parseFromArgs(args: ['/asdf', '--exact']);
      expect(parsedManifest.testBundles, hasLength(1));
      expect(parsedManifest.testBundles[0].testDefinition.path, '/asdf');

      // --exact kills partial path matches
      parsedManifest = parseFromArgs(args: ['asdf', '--exact']);
      expect(parsedManifest.testBundles, hasLength(0));
    });

    test('when the --exact flag is passed for a test packageUrl', () {
      // --exact correctly catches exact packageUrl matches
      ParsedManifest parsedManifest = parseFromArgs(args: [
        'fuchsia-pkg://fuchsia.com/fancy#superBigTest.cmx',
        '--exact'
      ]);
      expect(parsedManifest.testBundles, hasLength(1));
      expect(parsedManifest.testBundles[0].testDefinition.name, 'device test');

      // --exact kills partial packageUrl matches
      parsedManifest =
          parseFromArgs(args: ['fuchsia-pkg://fuchsia.com/fancy', '--exact']);
      expect(parsedManifest.testBundles, hasLength(0));
    });

    test('when the -h flag is passed', () {
      ParsedManifest parsedManifest = parseFromArgs(args: ['//host/test']);
      expect(parsedManifest.testBundles, hasLength(1));
      expect(parsedManifest.testBundles[0].testDefinition.name, '//host/test');
    });

    test('when the -d flag is passed', () {
      ParsedManifest parsedManifest = parseFromArgs(
        args: ['fuchsia-pkg://fuchsia.com/fancy#superBigTest.cmx', '--device'],
      );
      expect(parsedManifest.testBundles, hasLength(1));
      expect(parsedManifest.testBundles[0].testDefinition.name, 'device test');
    });

    test('when no flags are passed', () {
      ParsedManifest parsedManifest = parseFromArgs();
      expect(parsedManifest.testBundles, hasLength(2));
    });

    test('when packageUrl.resourcePath is matched', () {
      ParsedManifest parsedManifest = parseFromArgs(args: ['superBigTest.cmx']);
      expect(parsedManifest.testBundles, hasLength(1));
      expect(parsedManifest.testBundles[0].testDefinition.name, 'device test');
    });
    test('when packageUrl.rawResource is matched', () {
      ParsedManifest parsedManifest = parseFromArgs(args: ['superBigTest']);
      expect(parsedManifest.testBundles, hasLength(1));
      expect(parsedManifest.testBundles[0].testDefinition.name, 'device test');
    });

    test('when packageUrl.resourcePath are not components', () {
      expect(
        () => TestDefinition(
          buildDir: fuchsiaLocator.buildDir,
          fx: fuchsiaLocator.fx,
          os: 'fuchsia',
          packageUrl: 'fuchsia-pkg://fuchsia.com/fancy#meta/not-component',
          name: 'asdf-one',
        ),
        throwsA(TypeMatcher<MalformedFuchsiaUrlException>()),
      );
      expect(
        () => TestDefinition(
          buildDir: fuchsiaLocator.buildDir,
          fx: fuchsiaLocator.fx,
          os: 'fuchsia',
          packageUrl: 'fuchsia-pkg://fuchsia.com/fancy#bin/def-not-comp.so',
          name: 'asdf-two',
        ),
        throwsA(TypeMatcher<MalformedFuchsiaUrlException>()),
      );
    });

    test('when packageUrl.packageName is matched', () {
      TestsConfig testsConfig = TestsConfig.all(tNames: ['fancy']);
      ParsedManifest parsedManifest = tr.aggregateTests(
        testDefinitions: testDefinitions,
        buildDir: fuchsiaLocator.buildDir,
        eventEmitter: _ignoreEvents,
        testsConfig: testsConfig,
        testRunner: testRunner,
      );
      expect(parsedManifest.testBundles, hasLength(1));
      expect(parsedManifest.testBundles[0].testDefinition.name, 'device test');
    });

    test(
        'when packageUrl.packageName is matched but discriminating '
        'flag prevents', () {
      TestsConfig testsConfig = TestsConfig.host(tNames: ['fancy']);
      ParsedManifest parsedManifest = tr.aggregateTests(
        testDefinitions: testDefinitions,
        buildDir: fuchsiaLocator.buildDir,
        eventEmitter: _ignoreEvents,
        testsConfig: testsConfig,
        testRunner: testRunner,
      );
      expect(parsedManifest.testBundles, hasLength(0));
    });

    test('when . is passed from the build dir', () {
      TestsConfig testsConfig = TestsConfig.host(tNames: ['.']);
      // Copy the list
      var tds = testDefinitions.sublist(0)
        ..addAll([
          TestDefinition(
            buildDir: fuchsiaLocator.buildDir,
            fx: fuchsiaLocator.fx,
            name: 'awesome host test',
            os: 'linux',
            path: 'host_x64/test',
          ),
        ]);
      ParsedManifest parsedManifest = tr.aggregateTests(
        testDefinitions: tds,
        buildDir: fuchsiaLocator.buildDir,
        eventEmitter: _ignoreEvents,
        testsConfig: testsConfig,
        testRunner: testRunner,
      );

      expect(parsedManifest.testBundles, hasLength(1));
      expect(
        parsedManifest.testBundles[0].testDefinition.name,
        'awesome host test',
      );
    });

    test('when . is passed from the build dir and there\'s device tests', () {
      TestsConfig testsConfig = TestsConfig.all(tNames: ['.']);
      // Copy the list
      var tds = testDefinitions.sublist(0)
        ..addAll([
          TestDefinition(
            buildDir: fuchsiaLocator.buildDir,
            fx: fuchsiaLocator.fx,
            name: 'awesome device test',
            os: 'fuchsia',
            path: '/pkgfs/stuff',
          ),
        ]);
      ParsedManifest parsedManifest = tr.aggregateTests(
        testDefinitions: tds,
        buildDir: fuchsiaLocator.buildDir,
        eventEmitter: _ignoreEvents,
        testsConfig: testsConfig,
        testRunner: testRunner,
      );

      expect(parsedManifest.testBundles, hasLength(0));
    });
  });

  group('fuchsia-package URLs are correctly parsed', () {
    test('when all components are present', () {
      PackageUrl packageUrl = PackageUrl.fromString(
          'fuchsia-pkg://myroot.com/pkg-name/VARIANT?hash=asdf#OMG.cmx');
      expect(packageUrl.host, 'myroot.com');
      expect(packageUrl.packageName, 'pkg-name');
      expect(packageUrl.packageVariant, 'VARIANT');
      expect(packageUrl.hash, 'asdf');
      expect(packageUrl.resourcePath, 'OMG.cmx');
      expect(packageUrl.rawResource, 'OMG');
    });

    test('when the variant is missing', () {
      PackageUrl packageUrl = PackageUrl.fromString(
          'fuchsia-pkg://myroot.com/pkg-name?hash=asdf#OMG.cmx');
      expect(packageUrl.host, 'myroot.com');
      expect(packageUrl.packageName, 'pkg-name');
      expect(packageUrl.packageVariant, null);
      expect(packageUrl.hash, 'asdf');
      expect(packageUrl.resourcePath, 'OMG.cmx');
      expect(packageUrl.rawResource, 'OMG');
    });

    test('when the hash is missing', () {
      PackageUrl packageUrl = PackageUrl.fromString(
          'fuchsia-pkg://myroot.com/pkg-name/VARIANT#OMG.cmx');
      expect(packageUrl.host, 'myroot.com');
      expect(packageUrl.packageName, 'pkg-name');
      expect(packageUrl.packageVariant, 'VARIANT');
      expect(packageUrl.hash, null);
      expect(packageUrl.resourcePath, 'OMG.cmx');
      expect(packageUrl.rawResource, 'OMG');
    });

    test('when the resource path is missing', () {
      PackageUrl packageUrl = PackageUrl.fromString(
          'fuchsia-pkg://myroot.com/pkg-name/VARIANT?hash=asdf');
      expect(packageUrl.host, 'myroot.com');
      expect(packageUrl.packageName, 'pkg-name');
      expect(packageUrl.packageVariant, 'VARIANT');
      expect(packageUrl.hash, 'asdf');
      expect(packageUrl.resourcePath, '');
      expect(packageUrl.rawResource, '');
    });

    test('when the variant and hash are missing', () {
      PackageUrl packageUrl =
          PackageUrl.fromString('fuchsia-pkg://myroot.com/pkg-name#OMG.cmx');
      expect(packageUrl.host, 'myroot.com');
      expect(packageUrl.packageName, 'pkg-name');
      expect(packageUrl.packageVariant, null);
      expect(packageUrl.hash, null);
      expect(packageUrl.resourcePath, 'OMG.cmx');
      expect(packageUrl.rawResource, 'OMG');
    });
    test('when the variant and resource path are missing', () {
      PackageUrl packageUrl =
          PackageUrl.fromString('fuchsia-pkg://myroot.com/pkg-name?hash=asdf');
      expect(packageUrl.host, 'myroot.com');
      expect(packageUrl.packageName, 'pkg-name');
      expect(packageUrl.packageVariant, null);
      expect(packageUrl.hash, 'asdf');
      expect(packageUrl.resourcePath, '');
      expect(packageUrl.rawResource, '');
    });

    test('when the hash and resource path are missing', () {
      PackageUrl packageUrl =
          PackageUrl.fromString('fuchsia-pkg://myroot.com/pkg-name/VARIANT');
      expect(packageUrl.host, 'myroot.com');
      expect(packageUrl.packageName, 'pkg-name');
      expect(packageUrl.packageVariant, 'VARIANT');
      expect(packageUrl.hash, null);
      expect(packageUrl.resourcePath, '');
      expect(packageUrl.rawResource, '');
    });

    test('when the variant, hash, and resource path are missing', () {
      PackageUrl packageUrl =
          PackageUrl.fromString('fuchsia-pkg://myroot.com/pkg-name');
      expect(packageUrl.host, 'myroot.com');
      expect(packageUrl.packageName, 'pkg-name');
      expect(packageUrl.packageVariant, null);
      expect(packageUrl.hash, null);
      expect(packageUrl.resourcePath, '');
      expect(packageUrl.rawResource, '');
    });
  });

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

  group('test analytics are reporting', () {
    var envReader = MockEnvReader();
    when(envReader.getEnv('FUCHSIA_DIR')).thenReturn('/root/path/fuchsia');
    var fuchsiaLocator = FuchsiaLocator(envReader: envReader);
    var testBundles = <TestBundle>[
      TestBundle(
        TestDefinition(
          buildDir: '/',
          command: ['asdf'],
          fx: fuchsiaLocator.fx,
          name: 'Big Test',
          os: 'linux',
        ),
        workingDirectory: '.',
        testRunner: FakeTestRunner.passing(),
      ),
    ];
    test('functions on real test runs', () async {
      var cmd = FuchsiaTestCommand(
        analyticsReporter: AnalyticsFaker(),
        fuchsiaLocator: fuchsiaLocator,
        outputFormatter: null,
        testsConfig: TestsConfig(
          flags: Flags(dryRun: false),
          passThroughTokens: [],
          runnerTokens: [],
          testNames: [],
        ),
      );
      await cmd.runTests(testBundles).forEach((event) {});
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
        outputFormatter: null,
        testsConfig: TestsConfig(
          flags: Flags(dryRun: true),
          passThroughTokens: [],
          runnerTokens: [],
          testNames: [],
        ),
      );
      await cmd.runTests(testBundles).forEach((event) {});
      await cmd.cleanUp();
      expect(
        // ignore: avoid_as
        (cmd.analyticsReporter as AnalyticsFaker).reportHistory,
        hasLength(0),
      );
    });
  });
  group('test output is routed correctly', () {
    test('when -o is passed', () async {
      var strings = <String>[];
      void addStrings(String s) {
        strings.add(s);
      }

      var runner = TestRunner();
      ProcessResult result = await runner.run(
        './test/output_tester.sh',
        [],
        workingDirectory: '.',
        realtimeOutputSink: addStrings,
      );
      await Future.delayed(Duration(milliseconds: 1));

      expect(strings.length, 2);
      expect(strings[0], 'line 1');
      expect(strings[1], 'line 2');
      expect(result.stdout, '');
    });

    test('when -o is not passed', () async {
      var runner = TestRunner();
      ProcessResult result = await runner.run(
        './test/output_tester.sh',
        [],
        workingDirectory: '.',
      );
      expect(result.stdout, 'line 1\nline 2\n');
    });
  });

  group('prechecks', () {
    test('raise errors if fx is missing', () {
      var logged = false;
      var cmdCli = FuchsiaTestCommandCli([''], usage: (parser) {});
      expect(
        () => cmdCli.preRunChecks(
          cmdCli.parsedArgs,
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
        cmdCli.parsedArgs,
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
}
