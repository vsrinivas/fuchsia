import 'package:fxtest/fxtest.dart';
import 'package:test/test.dart';

import 'fake_fx_env.dart';

void main() {
  group('tests.json entries are correctly parsed', () {
    test('for host tests', () {
      TestsManifestReader tr = TestsManifestReader();
      List<dynamic> testJson = [
        {
          'environments': [],
          'test': {
            'cpu': 'x64',
            'runtime_deps':
                'host_x64/gen/topaz/tools/doc_checker/doc_checker_tests.deps.json',
            'path': 'host_x64/doc_checker_tests',
            'name': '//topaz/tools/doc_checker:doc_checker_tests',
            'os': 'linux',
          }
        },
      ];
      List<TestDefinition> tds = tr.parseManifest(
        testJson: testJson,
        buildDir: FakeFxEnv.shared.outputDir,
        fxLocation: FakeFxEnv.shared.fx,
      );
      expect(tds, hasLength(1));
      expect(tds[0].packageUrl, null);
      expect(tds[0].runtimeDeps, testJson[0]['test']['runtime_deps']);
      expect(tds[0].path, testJson[0]['test']['path']);
      expect(tds[0].name, testJson[0]['test']['name']);
      expect(tds[0].cpu, testJson[0]['test']['cpu']);
      expect(tds[0].os, testJson[0]['test']['os']);
      expect(tds[0].testType, TestType.host);
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
            'name': '//src/sys/run_test_component/test:run_test_component_test',
            'os': 'fuchsia',
            'package_url':
                'fuchsia-pkg://fuchsia.com/run_test_component_test#meta/run_test_component_test.cm',
            'log_settings': {'max_severity': 'ERROR'},
          }
        },
      ];
      List<TestDefinition> tds = tr.parseManifest(
        testJson: testJson,
        buildDir: FakeFxEnv.shared.outputDir,
        fxLocation: FakeFxEnv.shared.fx,
      );
      expect(tds, hasLength(1));
      expect(tds[0].packageUrl.toString(), testJson[0]['test']['package_url']);
      expect(tds[0].runtimeDeps, '');
      expect(tds[0].path, testJson[0]['test']['path']);
      expect(tds[0].name, testJson[0]['test']['name']);
      expect(tds[0].cpu, testJson[0]['test']['cpu']);
      expect(tds[0].os, testJson[0]['test']['os']);
      expect(tds[0].maxLogSeverity,
          testJson[0]['test']['log_settings']['max_severity']);
      expect(tds[0].testType, TestType.suite);
    });

    test('for device tests with no max_severity', () {
      TestsManifestReader tr = TestsManifestReader();
      List<dynamic> testJson = [
        {
          'environments': [],
          'test': {
            'cpu': 'arm64',
            'path':
                '/pkgfs/packages/run_test_component_test/0/test/run_test_component_test',
            'name': '//src/sys/run_test_component/test:run_test_component_test',
            'os': 'fuchsia',
            'package_url':
                'fuchsia-pkg://fuchsia.com/run_test_component_test#meta/run_test_component_test.cm',
            'log_settings': {},
          }
        },
      ];
      List<TestDefinition> tds = tr.parseManifest(
        testJson: testJson,
        buildDir: FakeFxEnv.shared.outputDir,
        fxLocation: FakeFxEnv.shared.fx,
      );
      expect(tds, hasLength(1));
      expect(tds[0].packageUrl.toString(), testJson[0]['test']['package_url']);
      expect(tds[0].runtimeDeps, '');
      expect(tds[0].path, testJson[0]['test']['path']);
      expect(tds[0].name, testJson[0]['test']['name']);
      expect(tds[0].cpu, testJson[0]['test']['cpu']);
      expect(tds[0].os, testJson[0]['test']['os']);
      expect(tds[0].maxLogSeverity, null);
      expect(tds[0].testType, TestType.suite);
    });

    test('for device tests with no log_settings', () {
      TestsManifestReader tr = TestsManifestReader();
      List<dynamic> testJson = [
        {
          'environments': [],
          'test': {
            'cpu': 'arm64',
            'path':
                '/pkgfs/packages/run_test_component_test/0/test/run_test_component_test',
            'name': '//src/sys/run_test_component/test:run_test_component_test',
            'os': 'fuchsia',
            'package_url':
                'fuchsia-pkg://fuchsia.com/run_test_component_test#meta/run_test_component_test.cm',
          }
        },
      ];
      List<TestDefinition> tds = tr.parseManifest(
        testJson: testJson,
        buildDir: FakeFxEnv.shared.outputDir,
        fxLocation: FakeFxEnv.shared.fx,
      );
      expect(tds, hasLength(1));
      expect(tds[0].packageUrl.toString(), testJson[0]['test']['package_url']);
      expect(tds[0].runtimeDeps, '');
      expect(tds[0].path, testJson[0]['test']['path']);
      expect(tds[0].name, testJson[0]['test']['name']);
      expect(tds[0].cpu, testJson[0]['test']['cpu']);
      expect(tds[0].os, testJson[0]['test']['os']);
      expect(tds[0].maxLogSeverity, null);
      expect(tds[0].testType, TestType.suite);
    });

    test('for unsupported tests', () {
      TestsManifestReader tr = TestsManifestReader();
      List<dynamic> testJson = [
        {
          'environments': [],
          'test': {
            'cpu': 'arm64',
            'name': '//src/sys/run_test_component/test:run_test_component_test',
            'os': 'fuchsia',
          }
        },
        {
          'environments': [],
          'test': {
            'cpu': 'arm64',
            'path':
                '/pkgfs/packages/run_test_component_test/0/test/run_test_component_test',
            'name': '//src/sys/run_test_component/test:run_test_component_test',
            'os': 'fuchsia',
            'package_url':
                'fuchsia-pkg://fuchsia.com/run_test_component_test#meta/run_test_component_test.cmx',
          }
        },
      ];
      List<TestDefinition> tds = tr.parseManifest(
        testJson: testJson,
        buildDir: FakeFxEnv.shared.outputDir,
        fxLocation: FakeFxEnv.shared.fx,
      );
      expect(tds, hasLength(2));
      expect(tds[0].path, '');
      expect(tds[0].testType, TestType.unsupported);
      expect(tds[1].testType, TestType.unsupportedDeviceTest);
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
        buildDir: FakeFxEnv.shared.outputDir,
        fxLocation: FakeFxEnv.shared.fx,
      );
      expect(tds, hasLength(1));
      expect(tds[0].testType, TestType.unsupportedDeviceTest);
    });
  });

  group('tests are aggregated correctly', () {
    TestRunner buildTestRunner(TestsConfig testsConfig) => TestRunner();

    void _ignoreEvents(TestEvent _) {}
    TestsManifestReader tr = TestsManifestReader();
    List<TestDefinition> testDefinitions = [
      TestDefinition(
        buildDir: FakeFxEnv.shared.outputDir,
        os: 'fuchsia',
        packageUrl: PackageUrl.fromString(
            'fuchsia-pkg://fuchsia.com/another_package#meta/test_component_2.cm'),
        name: 'device_test_v2',
      ),
      TestDefinition(
        buildDir: FakeFxEnv.shared.outputDir,
        os: 'linux',
        path: 'host_x64/path',
        // In practice, host tests have identical paths and names, but we
        // differentiate them here to verify both are matchable.
        name: 'host_x64/name',
      ),
    ];

    // Helper function to parse lots of data for tests
    ParsedManifest parseFromArgs({
      List<String> args = const [],
      List<TestDefinition>? testDefs,
    }) {
      TestsConfig testsConfig = TestsConfig.fromRawArgs(
        rawArgs: args,
        fxEnv: FakeFxEnv.shared,
      );
      var cmd = FuchsiaTestCommand.fromConfig(
        testsConfig,
        testRunnerBuilder: buildTestRunner,
      );
      return tr.aggregateTests(
        eventEmitter: _ignoreEvents,
        matchLength: testsConfig.flags.matchLength,
        testBundleBuilder: cmd.testBundleBuilder,
        testsConfig: testsConfig,
        testDefinitions: testDefs ?? testDefinitions,
      );
    }

    test('when --exact is not specified for a test name', () {
      ParsedManifest parsedManifest = parseFromArgs(args: ['test_v2']);
      expect(parsedManifest.testBundles, hasLength(1));
      expect(
          parsedManifest.testBundles[0].testDefinition.name, 'device_test_v2');
    });

    test('when the --exact flag is passed for a test name', () {
      // --exact correctly catches exact name matches
      ParsedManifest parsedManifest =
          parseFromArgs(args: ['host_x64/name', '--exact']);
      expect(parsedManifest.testBundles, hasLength(1));
      expect(
          parsedManifest.testBundles[0].testDefinition.name, 'host_x64/name');

      // --exact kills partial name matches
      parsedManifest = parseFromArgs(args: ['host_x64', '--exact']);
      expect(parsedManifest.testBundles, hasLength(0));
    });

    test('when the --exact flag is passed for a test path', () {
      // --exact correctly catches exact path matches
      ParsedManifest parsedManifest =
          parseFromArgs(args: ['host_x64/path', '--exact']);
      expect(parsedManifest.testBundles, hasLength(1));
      expect(
          parsedManifest.testBundles[0].testDefinition.path, 'host_x64/path');

      // --exact kills partial path matches
      parsedManifest = parseFromArgs(args: ['host_x64', '--exact']);
      expect(parsedManifest.testBundles, hasLength(0));
    });

    test('when the --exact flag is passed for a test packageUrl', () {
      // --exact correctly catches exact packageUrl matches
      ParsedManifest parsedManifest = parseFromArgs(args: [
        'fuchsia-pkg://fuchsia.com/another_package#meta/test_component_2.cm',
        '--exact'
      ]);
      expect(parsedManifest.testBundles, hasLength(1));
      expect(
          parsedManifest.testBundles[0].testDefinition.name, 'device_test_v2');

      // --exact kills partial packageUrl matches
      parsedManifest = parseFromArgs(
          args: ['fuchsia-pkg://fuchsia.com/another_package', '--exact']);
      expect(parsedManifest.testBundles, hasLength(0));
    });

    test('when the -h flag is passed', () {
      ParsedManifest parsedManifest = parseFromArgs(args: ['host_x64/name']);
      expect(parsedManifest.testBundles, hasLength(1));
      expect(
          parsedManifest.testBundles[0].testDefinition.name, 'host_x64/name');
    });

    test('when the -d flag is passed', () {
      ParsedManifest parsedManifest = parseFromArgs(
        args: [
          'fuchsia-pkg://fuchsia.com/another_package#meta/test_component_2.cm',
          '--device'
        ],
      );
      expect(parsedManifest.testBundles, hasLength(1));
      expect(
          parsedManifest.testBundles[0].testDefinition.name, 'device_test_v2');
    });

    test('when no flags are passed', () {
      ParsedManifest parsedManifest = parseFromArgs();
      expect(parsedManifest.testBundles, hasLength(2));
    });

    test('when packageUrl.resourcePath is matched', () {
      ParsedManifest parsedManifest =
          parseFromArgs(args: ['test_component_2.cm']);
      expect(parsedManifest.testBundles, hasLength(1));
      expect(
          parsedManifest.testBundles[0].testDefinition.name, 'device_test_v2');
    });
    test('when packageUrl.rawResource is matched', () {
      ParsedManifest parsedManifest = parseFromArgs(args: ['test_component_2']);
      expect(parsedManifest.testBundles, hasLength(1));
      expect(
          parsedManifest.testBundles[0].testDefinition.name, 'device_test_v2');
    });

    test('when packageUrl.resourcePath are not components', () {
      expect(
        () => TestDefinition(
          buildDir: FakeFxEnv.shared.outputDir,
          os: 'fuchsia',
          packageUrl: PackageUrl.fromString(
              'fuchsia-pkg://fuchsia.com/fancy#meta/not-component'),
          name: 'asdf-one',
        ).createExecutionHandle(),
        throwsA(TypeMatcher<MalformedFuchsiaUrlException>()),
      );
      expect(
        () => TestDefinition(
          buildDir: FakeFxEnv.shared.outputDir,
          os: 'fuchsia',
          packageUrl: PackageUrl.fromString(
              'fuchsia-pkg://fuchsia.com/fancy#bin/def-not-comp.so'),
          name: 'asdf-two',
        ).createExecutionHandle(),
        throwsA(TypeMatcher<MalformedFuchsiaUrlException>()),
      );
    });

    test('when packageUrl.packageName is matched', () {
      TestsConfig testsConfig = TestsConfig.fromRawArgs(
        rawArgs: ['another_package'],
        fxEnv: FakeFxEnv.shared,
      );
      var cmd = FuchsiaTestCommand.fromConfig(
        testsConfig,
        testRunnerBuilder: buildTestRunner,
      );
      ParsedManifest parsedManifest = tr.aggregateTests(
        eventEmitter: _ignoreEvents,
        testBundleBuilder: cmd.testBundleBuilder,
        testDefinitions: testDefinitions,
        testsConfig: testsConfig,
      );
      expect(parsedManifest.testBundles, hasLength(1));
      expect(
          parsedManifest.testBundles[0].testDefinition.name, 'device_test_v2');
    });

    test(
        'when packageUrl.packageName is matched but discriminating '
        'flag prevents', () {
      TestsConfig testsConfig = TestsConfig.fromRawArgs(
        rawArgs: ['a_fancy_package', '--host'],
        fxEnv: FakeFxEnv.shared,
      );
      var cmd = FuchsiaTestCommand.fromConfig(
        testsConfig,
        testRunnerBuilder: buildTestRunner,
      );
      ParsedManifest parsedManifest = tr.aggregateTests(
        eventEmitter: _ignoreEvents,
        testBundleBuilder: cmd.testBundleBuilder,
        testDefinitions: testDefinitions,
        testsConfig: testsConfig,
      );
      expect(parsedManifest.testBundles, hasLength(0));
    });

    test('when . is passed from the build dir', () {
      TestsConfig testsConfig = TestsConfig.fromRawArgs(
        rawArgs: ['.', '--host'],
        fxEnv: FakeFxEnv(cwd: '/root/fuchsia/out/default'),
      );
      // Copy the list
      var tds = testDefinitions.sublist(0)
        ..addAll([
          TestDefinition(
            buildDir: FakeFxEnv.shared.outputDir,
            name: 'awesome host test',
            os: 'linux',
            path: 'host_x64/test',
          ),
        ]);
      var cmd = FuchsiaTestCommand.fromConfig(
        testsConfig,
        testRunnerBuilder: buildTestRunner,
      );
      ParsedManifest parsedManifest = tr.aggregateTests(
        eventEmitter: _ignoreEvents,
        testBundleBuilder: cmd.testBundleBuilder,
        testDefinitions: tds,
        testsConfig: testsConfig,
      );

      expect(parsedManifest.testBundles, hasLength(2));
      expect(
        parsedManifest.testBundles[0].testDefinition.name,
        'host_x64/name',
      );
      expect(
        parsedManifest.testBundles[1].testDefinition.name,
        'awesome host test',
      );
    });

    test('when . is passed from the build dir and there\'s device tests', () {
      TestsConfig testsConfig = TestsConfig.fromRawArgs(
        rawArgs: ['.'],
        fxEnv: FakeFxEnv(cwd: '/root/fuchsia/out/default'),
      );
      // Copy the list
      var tds = testDefinitions.sublist(0)
        ..addAll([
          TestDefinition(
            buildDir: FakeFxEnv.shared.outputDir,
            os: 'fuchsia',
            packageUrl: PackageUrl.fromString(
                'fuchsia-pkg://fuchsia.com/another_package#meta/test_component_3.cm'),
            name: 'device_test_3',
          ),
        ]);
      var cmd = FuchsiaTestCommand.fromConfig(
        testsConfig,
        testRunnerBuilder: buildTestRunner,
      );
      ParsedManifest parsedManifest = tr.aggregateTests(
        eventEmitter: _ignoreEvents,
        testBundleBuilder: cmd.testBundleBuilder,
        testDefinitions: tds,
        testsConfig: testsConfig,
      );

      expect(parsedManifest.testBundles, hasLength(1));
    });
    test('when the ends of paths are supplied', () {
      ParsedManifest parsedManifest = parseFromArgs(args: ['path']);
      expect(parsedManifest.testBundles, hasLength(1));
      expect(
          parsedManifest.testBundles[0].testDefinition.path, 'host_x64/path');
    });

    test('when no supplied arguments match', () {
      ParsedManifest parsedManifest = parseFromArgs(args: ['no-match']);
      expect(parsedManifest.testBundles, hasLength(0));
      expect(parsedManifest.unusedConfigs, hasLength(1));
    });

    test('when some supplied arguments match', () {
      ParsedManifest parsedManifest =
          parseFromArgs(args: ['another_package', 'no-match']);
      expect(parsedManifest.testBundles, hasLength(1));
      expect(parsedManifest.unusedConfigs, hasLength(1));
      expect(parsedManifest.unusedConfigs?[0].testNameGroup,
          equals([MatchableArgument.unrestricted('no-match')]));
    });

    test('when multiple arguments match the same test', () {
      ParsedManifest parsedManifest =
          parseFromArgs(args: ['another_package', 'test_component_2']);
      expect(parsedManifest.testBundles, hasLength(1));
      expect(parsedManifest.unusedConfigs, hasLength(0));
    });
  });

  group('test hints are generated correctly', () {
    var fxtestDef = TestDefinition.fromJson(
      {
        'environments': [],
        'test': {
          'cpu': 'x64',
          'label': '//scripts/fxtest:fxtest_tests(//build/toolchain:host_x64)',
          'name': 'fxtest_tests',
          'os': 'linux',
          'path': 'host_x64/fxtest_tests',
          'runtime_deps': 'host_x64/gen/scripts/fxtest/fxtest_tests.deps.json'
        }
      },
      buildDir: '/whatever',
    );
    var randomDef = TestDefinition.fromJson(
      {
        'environments': [],
        'test': {
          'cpu': 'x64',
          'label': '//path/whatever:whatever_tests(//build/toolchain:host_x64)',
          'name': 'whatever_tests',
          'os': 'linux',
          'path': 'host_x64/whatever_tests',
          'runtime_deps':
              'host_x64/gen/scripts/whatever/whatever_tests.deps.json'
        }
      },
      buildDir: '/whatever',
    );
    test('with a typo inside a path', () {
      var reader = TestsManifestReader();
      var config = TestsConfig.fromRawArgs(
        rawArgs: ['scripts/fxtets'],
        fxEnv: FakeFxEnv.shared,
      );
      var parsedManifest = reader.aggregateTests(
        comparer: FuzzyComparer(threshold: 3),
        eventEmitter: (TestEvent event) => null,
        matchLength: MatchLength.partial,
        testBundleBuilder: (TestDefinition _testDef, [double? confidence]) =>
            TestBundle.build(
          directoryBuilder: (String path, {required bool recursive}) => null,
          testDefinition: _testDef,
          testRunnerBuilder: (testsConfig) => TestRunner(),
          timeElapsedSink: (duration, cmd, output) => null,
          workingDirectory: '/whatever',
          testsConfig: config,
        ),
        testDefinitions: [fxtestDef, randomDef],
        testsConfig: config,
      );
      expect(parsedManifest.testBundles.length, 1);
      expect(parsedManifest.testBundles[0].testDefinition.name, 'fxtest_tests');
    });

    test('with an fxtest typo', () {
      var reader = TestsManifestReader();
      var config = TestsConfig.fromRawArgs(
        rawArgs: ['fxtest_tetss'],
        fxEnv: FakeFxEnv.shared,
      );
      var parsedManifest = reader.aggregateTests(
        comparer: FuzzyComparer(threshold: 3),
        eventEmitter: (TestEvent event) => null,
        matchLength: MatchLength.partial,
        testBundleBuilder: (TestDefinition _testDef, [double? confidence]) =>
            TestBundle.build(
          directoryBuilder: (String path, {required bool recursive}) => null,
          testDefinition: _testDef,
          testRunnerBuilder: (testsConfig) => TestRunner(),
          timeElapsedSink: (duration, cmd, output) => null,
          workingDirectory: '/whatever',
          testsConfig: config,
        ),
        testDefinitions: [fxtestDef, randomDef],
        testsConfig: config,
      );
      expect(parsedManifest.testBundles.length, 1);
    });
  });

  group('tests are aggregated correctly with the -a flag', () {
    TestRunner buildTestRunner(TestsConfig testsConfig) => TestRunner();

    void _ignoreEvents(TestEvent _) {}
    TestsManifestReader tr = TestsManifestReader();

    var tds = <TestDefinition>[
      TestDefinition(
        buildDir: FakeFxEnv.shared.outputDir,
        os: 'fuchsia',
        packageUrl: PackageUrl.fromString(
            'fuchsia-pkg://fuchsia.com/pkg1#meta/test1.cm'),
        name: 'pkg 1 test 1',
      ),
      TestDefinition(
        buildDir: FakeFxEnv.shared.outputDir,
        os: 'fuchsia',
        packageUrl:
            PackageUrl.fromString('fuchsia-pkg://fuchsia.com/pkg1#test2.cm'),
        name: 'pkg 1 test 2',
      ),
      TestDefinition(
        buildDir: FakeFxEnv.shared.outputDir,
        os: 'fuchsia',
        packageUrl:
            PackageUrl.fromString('fuchsia-pkg://fuchsia.com/pkg2#test1.cm'),
        name: 'pkg 2 test 1',
        path: '//gnsubtree',
      ),
      TestDefinition(
        buildDir: FakeFxEnv.shared.outputDir,
        os: 'linux',
        path: '/asdf',
        name: '//host/test',
      ),
    ];

    test('specifies a non-trailing component name with no package name', () {
      TestsConfig testsConfig = TestsConfig.fromRawArgs(
        rawArgs: ['-c', 'test2', '//host/test'],
        fxEnv: FakeFxEnv.shared,
      );
      var cmd = FuchsiaTestCommand.fromConfig(
        testsConfig,
        testRunnerBuilder: buildTestRunner,
      );
      ParsedManifest parsedManifest = tr.aggregateTests(
        eventEmitter: _ignoreEvents,
        testBundleBuilder: cmd.testBundleBuilder,
        testDefinitions: tds,
        testsConfig: testsConfig,
      );

      var bundles = parsedManifest.testBundles;
      expect(bundles, hasLength(2));
      expect(bundles[0].testDefinition.name, 'pkg 1 test 2');
      expect(bundles[1].testDefinition.name, '//host/test');
    });

    test('specifies an impossible combination of two valid filters', () {
      TestsConfig testsConfig = TestsConfig.fromRawArgs(
        rawArgs: ['pkg1', '-a', '//host/test'],
        fxEnv: FakeFxEnv.shared,
      );
      var cmd = FuchsiaTestCommand.fromConfig(
        testsConfig,
        testRunnerBuilder: buildTestRunner,
      );
      ParsedManifest parsedManifest = tr.aggregateTests(
        eventEmitter: _ignoreEvents,
        testBundleBuilder: cmd.testBundleBuilder,
        testDefinitions: tds,
        testsConfig: testsConfig,
      );

      expect(parsedManifest.testBundles, hasLength(0));
    });

    test('is not present to remove other pkg matches', () {
      TestsConfig testsConfig = TestsConfig.fromRawArgs(
        rawArgs: ['pkg1'],
        fxEnv: FakeFxEnv.shared,
      );
      var cmd = FuchsiaTestCommand.fromConfig(
        testsConfig,
        testRunnerBuilder: buildTestRunner,
      );
      ParsedManifest parsedManifest = tr.aggregateTests(
        eventEmitter: _ignoreEvents,
        testBundleBuilder: cmd.testBundleBuilder,
        testDefinitions: tds,
        testsConfig: testsConfig,
      );
      var bundles = parsedManifest.testBundles;

      expect(bundles, hasLength(2));
      expect(bundles[0].testDefinition.name, 'pkg 1 test 1');
      expect(bundles[1].testDefinition.name, 'pkg 1 test 2');
    });

    test('combines filters from different fields', () {
      TestsConfig testsConfig = TestsConfig.fromRawArgs(
        rawArgs: ['//gnsubtree', '-a', 'test1'],
        fxEnv: FakeFxEnv.shared,
      );
      var cmd = FuchsiaTestCommand.fromConfig(
        testsConfig,
        testRunnerBuilder: buildTestRunner,
      );
      ParsedManifest parsedManifest = tr.aggregateTests(
        eventEmitter: _ignoreEvents,
        testBundleBuilder: cmd.testBundleBuilder,
        testDefinitions: tds,
        testsConfig: testsConfig,
      );
      var bundles = parsedManifest.testBundles;

      expect(bundles, hasLength(1));
      expect(bundles[0].testDefinition.name, 'pkg 2 test 1');
    });

    test('is not present to remove other component matches', () {
      TestsConfig testsConfig = TestsConfig.fromRawArgs(
        rawArgs: ['test1'],
        fxEnv: FakeFxEnv.shared,
      );
      var cmd = FuchsiaTestCommand.fromConfig(
        testsConfig,
        testRunnerBuilder: buildTestRunner,
      );
      ParsedManifest parsedManifest = tr.aggregateTests(
        eventEmitter: _ignoreEvents,
        testBundleBuilder: cmd.testBundleBuilder,
        testDefinitions: tds,
        testsConfig: testsConfig,
      );
      var bundles = parsedManifest.testBundles;

      expect(bundles, hasLength(2));
      expect(bundles[0].testDefinition.name, 'pkg 1 test 1');
      expect(bundles[1].testDefinition.name, 'pkg 2 test 1');
    });

    test('when it removes other matches', () {
      TestsConfig testsConfig = TestsConfig.fromRawArgs(
        // `-a` flag will filter out `test1`
        rawArgs: ['pkg1', '-a', 'test2', '//host/test'],
        fxEnv: FakeFxEnv.shared,
      );
      var cmd = FuchsiaTestCommand.fromConfig(
        testsConfig,
        testRunnerBuilder: buildTestRunner,
      );
      ParsedManifest parsedManifest = tr.aggregateTests(
        eventEmitter: _ignoreEvents,
        testBundleBuilder: cmd.testBundleBuilder,
        testDefinitions: tds,
        testsConfig: testsConfig,
      );
      var bundles = parsedManifest.testBundles;

      expect(bundles, hasLength(2));
      expect(bundles[0].testDefinition.name, 'pkg 1 test 2');
      expect(bundles[1].testDefinition.name, '//host/test');
    });
  });

  group('command parameters are used', () {
    test('for e2e tests', () {
      var testData = <String, dynamic>{
        'environments': [
          {
            'dimensions': {'device_type': 'asdf'},
          }
        ],
        'test': {
          'command': [
            '--special',
            '--flags',
          ],
          'cpu': 'x64',
          'label': '//src/tests/pkg-name:test-name(//build/toolchain)',
          'name': '//src/tests/pkg-name:test-name',
          'os': 'linux',
          'path': 'host_x64/test-name',
        }
      };
      List<TestDefinition> testDefinitions =
          TestsManifestReader().parseManifest(
        testJson: [testData],
        buildDir: '/whatever',
        fxLocation: '/whatever/fx',
      );
      expect(testDefinitions[0].testType, TestType.e2e);

      var commandTokens =
          testDefinitions[0].createExecutionHandle().getInvocationTokens([]);
      var fullCommand = commandTokens.fullCommandDisplay([]);
      expect(fullCommand, contains('--special'));
      expect(fullCommand, contains('--flags'));
    });
  });

  group('TestEnvironment objects are parsed correctly', () {
    // Pass this into `buildTest` when you want an empty case. This simplifies
    // null-checking in the test.
    const emptyEnvs = <Map<String, dynamic>>[];
    const testJson = <String, dynamic>{
      'environments': emptyEnvs,
      'test': {
        'cpu': 'x64',
        'label': '//src/tests/pkg-name:test-name(//build/toolchain)',
        'name': '//src/tests/pkg-name:test-name',
        'os': 'linux',
        'path': 'host_x64/test-name',
      }
    };

    TestDefinition buildTest(List<Map<String, dynamic>> environments) =>
        TestDefinition.fromJson(
          Map<String, dynamic>.from(testJson)
            ..update('environments', (current) => environments),
          buildDir: 'whatever',
        );

    test('when the values are empty', () {
      final testDef = buildTest(emptyEnvs);
      expect(testDef.testEnvironments, hasLength(0));
      expect(testDef.isE2E, false);
    });
    test('when the values re-specify the host', () {
      final testDef = buildTest(<Map<String, dynamic>>[
        {
          'dimensions': {'os': 'linux'}
        }
      ]);
      expect(testDef.testEnvironments, hasLength(1));
      expect(testDef.testEnvironments.first.isDefined, true);
      expect(testDef.testEnvironments.first.os, 'linux');
      expect(testDef.testEnvironments.first.deviceDimension, null);
      expect(testDef.isE2E, false);
    });

    test('when the values re-specify the host with capitalization', () {
      final testDef = buildTest(<Map<String, dynamic>>[
        {
          'dimensions': {'os': 'Linux'}
        }
      ]);
      expect(testDef.testEnvironments, hasLength(1));
      expect(testDef.testEnvironments.first.isDefined, true);
      expect(testDef.testEnvironments.first.os, 'Linux');
      expect(testDef.testEnvironments.first.deviceDimension, null);
      expect(testDef.isE2E, false);
    });

    test('when the values specify a device', () {
      final testDef = buildTest(<Map<String, dynamic>>[
        {
          'dimensions': {'device_type': 'asdf'}
        }
      ]);
      expect(testDef.testEnvironments, hasLength(1));
      expect(testDef.testEnvironments.first.isDefined, true);
      expect(testDef.testEnvironments.first.os, null);
      expect(testDef.testEnvironments.first.deviceDimension, 'asdf');
      expect(testDef.isE2E, true);
    });

    test('when the values specify a non-host os', () {
      final testDef = buildTest(<Map<String, dynamic>>[
        {
          'dimensions': {'os': 'fuchsia'}
        }
      ]);
      expect(testDef.testEnvironments, hasLength(1));
      expect(testDef.testEnvironments.first.isDefined, true);
      expect(testDef.testEnvironments.first.os, 'fuchsia');
      expect(testDef.testEnvironments.first.deviceDimension, null);
      expect(testDef.isE2E, true);
    });

    test('when the values specify a non-host os with capitalization', () {
      final testDef = buildTest(<Map<String, dynamic>>[
        {
          'dimensions': {'os': 'Fuchsia'}
        }
      ]);
      expect(testDef.testEnvironments, hasLength(1));
      expect(testDef.testEnvironments.first.isDefined, true);
      expect(testDef.testEnvironments.first.os, 'Fuchsia');
      expect(testDef.testEnvironments.first.deviceDimension, null);
      expect(testDef.isE2E, true);
    });
  });
}
