import 'package:test/test.dart';
import 'package:fxtest/fxtest.dart';
import 'package:mockito/mockito.dart';

// Mock this because it checks environment variables
class MockEnvReader extends Mock implements EnvReader {}

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
        packageUrl: 'fuchsia-pkg://fuchsia.com/fancy#meta/test1.cmx',
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
      var testNamesCollector = TestNamesCollector(
        rawTestNames: parsedArgs.rest,
        rawArgs: parsedArgs.arguments,
      );
      testsConfig = TestsConfig.fromArgResults(
        results: parsedArgs,
        testNameGroups: testNamesCollector.collect(),
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
      ParsedManifest parsedManifest = parseFromArgs(
          args: ['fuchsia-pkg://fuchsia.com/fancy#meta/test1.cmx', '--exact']);
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
        args: ['fuchsia-pkg://fuchsia.com/fancy#meta/test1.cmx', '--device'],
      );
      expect(parsedManifest.testBundles, hasLength(1));
      expect(parsedManifest.testBundles[0].testDefinition.name, 'device test');
    });

    test('when no flags are passed', () {
      ParsedManifest parsedManifest = parseFromArgs();
      expect(parsedManifest.testBundles, hasLength(2));
    });

    test('when packageUrl.resourcePath is matched', () {
      ParsedManifest parsedManifest = parseFromArgs(args: ['test1.cmx']);
      expect(parsedManifest.testBundles, hasLength(1));
      expect(parsedManifest.testBundles[0].testDefinition.name, 'device test');
    });
    test('when packageUrl.rawResource is matched', () {
      ParsedManifest parsedManifest = parseFromArgs(args: ['test1']);
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
      TestsConfig testsConfig = TestsConfig.all(testNameGroups: [
        ['fancy']
      ]);
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
      TestsConfig testsConfig = TestsConfig.host(testNameGroups: [
        ['fancy']
      ]);
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
      TestsConfig testsConfig = TestsConfig.host(testNameGroups: [
        ['.']
      ]);
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
      TestsConfig testsConfig = TestsConfig.all(testNameGroups: [
        ['.']
      ]);
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

  group('tests are aggregated correctly with the -a flag', () {
    var envReader = MockEnvReader();
    when(envReader.getEnv('FUCHSIA_DIR')).thenReturn('/root/path/fuchsia');
    when(envReader.getEnv('FUCHSIA_BUILD_DIR'))
        .thenReturn('/root/path/fuchsia/out/default');
    var fuchsiaLocator = FuchsiaLocator(envReader: envReader);

    void _ignoreEvents(TestEvent _) {}
    TestsManifestReader tr = TestsManifestReader();

    var tds = <TestDefinition>[
      TestDefinition(
        buildDir: fuchsiaLocator.buildDir,
        os: 'fuchsia',
        fx: fuchsiaLocator.fx,
        packageUrl: 'fuchsia-pkg://fuchsia.com/pkg1#meta/test1.cmx',
        name: 'pkg 1 test 1',
      ),
      TestDefinition(
        buildDir: fuchsiaLocator.buildDir,
        os: 'fuchsia',
        fx: fuchsiaLocator.fx,
        packageUrl: 'fuchsia-pkg://fuchsia.com/pkg1#test2.cmx',
        name: 'pkg 1 test 2',
      ),
      TestDefinition(
        buildDir: fuchsiaLocator.buildDir,
        os: 'fuchsia',
        fx: fuchsiaLocator.fx,
        packageUrl: 'fuchsia-pkg://fuchsia.com/pkg2#test1.cmx',
        name: 'pkg 2 test 1',
        path: '//gnsubtree',
      ),
      TestDefinition(
        buildDir: fuchsiaLocator.buildDir,
        fx: fuchsiaLocator.fx,
        os: 'linux',
        path: '/asdf',
        name: '//host/test',
      ),
    ];

    test('specifies an impossible combination of two valid filters', () {
      var collector = TestNamesCollector(
        rawArgs: ['pkg1', '-a', '//host/test'],
        rawTestNames: ['pkg1'],
      );
      TestsConfig testsConfig = TestsConfig.all(
        testNameGroups: collector.collect(),
      );
      ParsedManifest parsedManifest = tr.aggregateTests(
        testDefinitions: tds,
        buildDir: fuchsiaLocator.buildDir,
        eventEmitter: _ignoreEvents,
        testsConfig: testsConfig,
        testRunner: TestRunner(),
      );

      expect(parsedManifest.testBundles, hasLength(0));
    });

    test('is not present to remove other pkg matches', () {
      var collector = TestNamesCollector(
        rawArgs: ['pkg1'],
        rawTestNames: ['pkg1'],
      );
      TestsConfig testsConfig = TestsConfig.all(
        testNameGroups: collector.collect(),
      );
      ParsedManifest parsedManifest = tr.aggregateTests(
        testDefinitions: tds,
        buildDir: fuchsiaLocator.buildDir,
        eventEmitter: _ignoreEvents,
        testsConfig: testsConfig,
        testRunner: TestRunner(),
      );
      var bundles = parsedManifest.testBundles;

      expect(bundles, hasLength(2));
      expect(bundles[0].testDefinition.name, 'pkg 1 test 1');
      expect(bundles[1].testDefinition.name, 'pkg 1 test 2');
    });

    test('combines filters from different fields', () {
      var collector = TestNamesCollector(
        rawArgs: ['//gnsubtree', '-a', 'test1'],
        rawTestNames: ['//gnsubtree'],
      );
      TestsConfig testsConfig = TestsConfig.all(
        testNameGroups: collector.collect(),
      );
      ParsedManifest parsedManifest = tr.aggregateTests(
        testDefinitions: tds,
        buildDir: fuchsiaLocator.buildDir,
        eventEmitter: _ignoreEvents,
        testsConfig: testsConfig,
        testRunner: TestRunner(),
      );
      var bundles = parsedManifest.testBundles;

      expect(bundles, hasLength(1));
      expect(bundles[0].testDefinition.name, 'pkg 2 test 1');
    });

    test('is not present to remove other component matches', () {
      var collector = TestNamesCollector(
        rawArgs: ['test1'],
        rawTestNames: ['test1'],
      );
      TestsConfig testsConfig = TestsConfig.all(
        testNameGroups: collector.collect(),
      );
      ParsedManifest parsedManifest = tr.aggregateTests(
        testDefinitions: tds,
        buildDir: fuchsiaLocator.buildDir,
        eventEmitter: _ignoreEvents,
        testsConfig: testsConfig,
        testRunner: TestRunner(),
      );
      var bundles = parsedManifest.testBundles;

      expect(bundles, hasLength(2));
      expect(bundles[0].testDefinition.name, 'pkg 1 test 1');
      expect(bundles[1].testDefinition.name, 'pkg 2 test 1');
    });

    test('when it removes other matches', () {
      var collector = TestNamesCollector(
        // `-a` flag will filter out `test1`
        rawArgs: ['pkg1', '-a', 'test2', '//host/test'],
        rawTestNames: ['pkg1', '//host/test'],
      );
      TestsConfig testsConfig = TestsConfig.all(
        testNameGroups: collector.collect(),
      );
      ParsedManifest parsedManifest = tr.aggregateTests(
        testDefinitions: tds,
        buildDir: fuchsiaLocator.buildDir,
        eventEmitter: _ignoreEvents,
        testsConfig: testsConfig,
        testRunner: TestRunner(),
      );
      var bundles = parsedManifest.testBundles;

      expect(bundles, hasLength(2));
      expect(bundles[0].testDefinition.name, 'pkg 1 test 2');
      expect(bundles[1].testDefinition.name, '//host/test');
    });
  });
}
