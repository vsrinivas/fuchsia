import 'dart:io';
import 'package:async/async.dart';
import 'package:path/path.dart' as p;
import 'package:fxtest/fxtest.dart';
import 'package:mockito/mockito.dart';
import 'package:test/test.dart';

// Mock this because it checks environment variables
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
    String workingDirectory,
  }) async {
    return Future.value(
      ProcessResult(1, 0, args.join(' '), workingDirectory.toString()),
    );
  }
}

void main() {
  // out of test, out of fxtest, out of scripts
  // Alternatively, we could read the FUCHSIA_BUILD_DIR env variable,
  // but we're trying to have this code remain env-variable independent
  String buildDir = p.join('..', '..', '..', 'out/default');
  group('tests.json entries are correctly parsed', () {
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
      List<TestDefinition> tds = tr.parseManifest(testJson, buildDir);
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
      List<TestDefinition> tds = tr.parseManifest(testJson, buildDir);
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
      List<TestDefinition> tds = tr.parseManifest(testJson, buildDir);
      expect(tds, hasLength(1));
      expect(tds[0].path, '');
      expect(tds[0].executionHandle.testType, TestType.unsupported);
    });
  });

  group('tests are aggregated correctly', () {
    void _ignoreEvents(TestEvent _) {}
    TestsManifestReader tr = TestsManifestReader();
    List<TestDefinition> testDefinitions = [
      TestDefinition(
        buildDir: buildDir,
        os: 'fuchsia',
        packageUrl: 'fuchsia-pkg://fuchsia.com/fancy#test.cmx',
        name: 'device test',
      ),
      TestDefinition(
        buildDir: buildDir,
        os: 'linux',
        path: '/asdf',
        name: '//host/test',
      ),
    ];
    test('when the -h flag is passed', () {
      TestsConfig testsConfig = TestsConfig.host(['//host/test']);
      ParsedManifest parsedManifest = tr.aggregateTests(
        testDefinitions: testDefinitions,
        buildDir: buildDir,
        eventEmitter: _ignoreEvents,
        testsConfig: testsConfig,
      );
      expect(parsedManifest.testBundles, hasLength(1));
      expect(parsedManifest.testBundles[0].testDefinition.name, '//host/test');
    });

    test('when the -d flag is passed', () {
      TestsConfig testsConfig =
          TestsConfig.device(['fuchsia-pkg://fuchsia.com/fancy#test.cmx']);
      ParsedManifest parsedManifest = tr.aggregateTests(
        testDefinitions: testDefinitions,
        buildDir: buildDir,
        eventEmitter: _ignoreEvents,
        testsConfig: testsConfig,
      );
      expect(parsedManifest.testBundles, hasLength(1));
      expect(parsedManifest.testBundles[0].testDefinition.name, 'device test');
    });

    test('when no flags are passed', () {
      TestsConfig testsConfig = TestsConfig.all();
      ParsedManifest parsedManifest = tr.aggregateTests(
        testDefinitions: testDefinitions,
        buildDir: buildDir,
        eventEmitter: _ignoreEvents,
        testsConfig: testsConfig,
      );
      expect(parsedManifest.testBundles, hasLength(2));
    });

    test('when packageUrl `name` is matched', () {
      TestsConfig testsConfig = TestsConfig.all(['fancy']);
      ParsedManifest parsedManifest = tr.aggregateTests(
        testDefinitions: testDefinitions,
        buildDir: buildDir,
        eventEmitter: _ignoreEvents,
        testsConfig: testsConfig,
      );
      expect(parsedManifest.testBundles, hasLength(1));
      expect(parsedManifest.testBundles[0].testDefinition.name, 'device test');
    });

    test('when packageUrl `name` is matched but discriminating flag prevents',
        () {
      TestsConfig testsConfig = TestsConfig.host(['fancy']);
      ParsedManifest parsedManifest = tr.aggregateTests(
        testDefinitions: testDefinitions,
        buildDir: buildDir,
        eventEmitter: _ignoreEvents,
        testsConfig: testsConfig,
      );
      expect(parsedManifest.testBundles, hasLength(0));
    });

    test('when . is passed from the build dir', () {
      TestsConfig testsConfig = TestsConfig.host(['.']);
      // Copy the list
      var tds = testDefinitions.sublist(0)
        ..addAll([
          TestDefinition(
            os: 'linux',
            name: 'awesome host test',
            path: 'host_x64/test',
            buildDir: buildDir,
          ),
        ]);
      ParsedManifest parsedManifest = tr.aggregateTests(
        testDefinitions: tds,
        buildDir: buildDir,
        eventEmitter: _ignoreEvents,
        testsConfig: testsConfig,
      );

      expect(parsedManifest.testBundles, hasLength(1));
      expect(
        parsedManifest.testBundles[0].testDefinition.name,
        'awesome host test',
      );
    });

    test('when . is passed from the build dir and there\'s device tests', () {
      TestsConfig testsConfig = TestsConfig.all(['.']);
      // Copy the list
      var tds = testDefinitions.sublist(0)
        ..addAll([
          TestDefinition(
            os: 'fuchsia',
            name: 'awesome device test',
            path: '/pkgfs/stuff',
            buildDir: buildDir,
          ),
        ]);
      ParsedManifest parsedManifest = tr.aggregateTests(
        testDefinitions: tds,
        buildDir: buildDir,
        eventEmitter: _ignoreEvents,
        testsConfig: testsConfig,
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
    });

    test('when the variant is missing', () {
      PackageUrl packageUrl = PackageUrl.fromString(
          'fuchsia-pkg://myroot.com/pkg-name?hash=asdf#OMG.cmx');
      expect(packageUrl.host, 'myroot.com');
      expect(packageUrl.packageName, 'pkg-name');
      expect(packageUrl.packageVariant, null);
      expect(packageUrl.hash, 'asdf');
      expect(packageUrl.resourcePath, 'OMG.cmx');
    });

    test('when the hash is missing', () {
      PackageUrl packageUrl = PackageUrl.fromString(
          'fuchsia-pkg://myroot.com/pkg-name/VARIANT#OMG.cmx');
      expect(packageUrl.host, 'myroot.com');
      expect(packageUrl.packageName, 'pkg-name');
      expect(packageUrl.packageVariant, 'VARIANT');
      expect(packageUrl.hash, null);
      expect(packageUrl.resourcePath, 'OMG.cmx');
    });

    test('when the resource path is missing', () {
      PackageUrl packageUrl = PackageUrl.fromString(
          'fuchsia-pkg://myroot.com/pkg-name/VARIANT?hash=asdf');
      expect(packageUrl.host, 'myroot.com');
      expect(packageUrl.packageName, 'pkg-name');
      expect(packageUrl.packageVariant, 'VARIANT');
      expect(packageUrl.hash, 'asdf');
      expect(packageUrl.resourcePath, '');
    });

    test('when the variant and hash are missing', () {
      PackageUrl packageUrl =
          PackageUrl.fromString('fuchsia-pkg://myroot.com/pkg-name#OMG.cmx');
      expect(packageUrl.host, 'myroot.com');
      expect(packageUrl.packageName, 'pkg-name');
      expect(packageUrl.packageVariant, null);
      expect(packageUrl.hash, null);
      expect(packageUrl.resourcePath, 'OMG.cmx');
    });
    test('when the variant and resource path are missing', () {
      PackageUrl packageUrl =
          PackageUrl.fromString('fuchsia-pkg://myroot.com/pkg-name?hash=asdf');
      expect(packageUrl.host, 'myroot.com');
      expect(packageUrl.packageName, 'pkg-name');
      expect(packageUrl.packageVariant, null);
      expect(packageUrl.hash, 'asdf');
      expect(packageUrl.resourcePath, '');
    });

    test('when the hash and resource path are missing', () {
      PackageUrl packageUrl =
          PackageUrl.fromString('fuchsia-pkg://myroot.com/pkg-name/VARIANT');
      expect(packageUrl.host, 'myroot.com');
      expect(packageUrl.packageName, 'pkg-name');
      expect(packageUrl.packageVariant, 'VARIANT');
      expect(packageUrl.hash, null);
      expect(packageUrl.resourcePath, '');
    });

    test('when the variant, hash, and resource path are missing', () {
      PackageUrl packageUrl =
          PackageUrl.fromString('fuchsia-pkg://myroot.com/pkg-name');
      expect(packageUrl.host, 'myroot.com');
      expect(packageUrl.packageName, 'pkg-name');
      expect(packageUrl.packageVariant, null);
      expect(packageUrl.hash, null);
      expect(packageUrl.resourcePath, '');
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
    void _ignoreEvents(TestEvent _) {}
    TestsManifestReader tr = TestsManifestReader();
    List<TestDefinition> testDefinitions = [
      TestDefinition(
        buildDir: buildDir,
        os: 'fuchsia',
        packageUrl: 'fuchsia-pkg://fuchsia.com/fancy#test.cmx',
        name: 'device test',
      ),
      TestDefinition(
        buildDir: buildDir,
        os: 'linux',
        path: '/asdf',
        name: 'example test',
      ),
    ];

    test('when there are pass-thru commands', () async {
      var testsConfig = TestsConfig(
        flags: Flags.defaults(),
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
  });
}
