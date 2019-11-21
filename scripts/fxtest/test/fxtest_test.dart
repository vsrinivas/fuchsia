import 'package:path/path.dart' as p;
import 'package:fxtest/fxtest.dart';
import 'package:test/test.dart';

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
      TestFlags testFlags = TestFlags.host(['//host/test']);
      ParsedManifest parsedManifest = tr.aggregateTests(
        testDefinitions: testDefinitions,
        buildDir: buildDir,
        eventEmitter: _ignoreEvents,
        testFlags: testFlags,
      );
      expect(parsedManifest.testRunners, hasLength(1));
      expect(parsedManifest.testRunners[0].testDefinition.name, '//host/test');
    });

    test('when the -d flag is passed', () {
      TestFlags testFlags =
          TestFlags.device(['fuchsia-pkg://fuchsia.com/fancy#test.cmx']);
      ParsedManifest parsedManifest = tr.aggregateTests(
        testDefinitions: testDefinitions,
        buildDir: buildDir,
        eventEmitter: _ignoreEvents,
        testFlags: testFlags,
      );
      expect(parsedManifest.testRunners, hasLength(1));
      expect(parsedManifest.testRunners[0].testDefinition.name, 'device test');
    });

    test('when no flags are passed', () {
      TestFlags testFlags = TestFlags.all();
      ParsedManifest parsedManifest = tr.aggregateTests(
        testDefinitions: testDefinitions,
        buildDir: buildDir,
        eventEmitter: _ignoreEvents,
        testFlags: testFlags,
      );
      expect(parsedManifest.testRunners, hasLength(2));
    });

    test('when packageUrl `name` is matched', () {
      TestFlags testFlags = TestFlags.all(['fancy']);
      ParsedManifest parsedManifest = tr.aggregateTests(
        testDefinitions: testDefinitions,
        buildDir: buildDir,
        eventEmitter: _ignoreEvents,
        testFlags: testFlags,
      );
      expect(parsedManifest.testRunners, hasLength(1));
      expect(parsedManifest.testRunners[0].testDefinition.name, 'device test');
    });

    test('when packageUrl `name` is matched but discriminating flag prevents',
        () {
      TestFlags testFlags = TestFlags.host(['fancy']);
      ParsedManifest parsedManifest = tr.aggregateTests(
        testDefinitions: testDefinitions,
        buildDir: buildDir,
        eventEmitter: _ignoreEvents,
        testFlags: testFlags,
      );
      expect(parsedManifest.testRunners, hasLength(0));
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
}
