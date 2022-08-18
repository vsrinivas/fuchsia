// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:fxtest/fxtest.dart';
import 'package:fxutils/fxutils.dart';
import 'package:mockito/mockito.dart';
import 'package:test/test.dart';
import 'dart:io';
import 'dart:convert';

import 'fake_fx_env.dart';

class MockEnvReader extends Mock implements EnvReader {}

void main() {
  group('package repository', () {
    TestRunner buildTestRunner(TestsConfig testsConfig) => TestRunner();
    test('not used when there are no component tests', () async {
      var testsConfig = TestsConfig.fromRawArgs(
        rawArgs: [],
        fxEnv: FakeFxEnv.shared,
      );
      var cmd = FuchsiaTestCommand.fromConfig(
        testsConfig,
        testRunnerBuilder: buildTestRunner,
      );
      final bundle = cmd.testBundleBuilder(TestDefinition.fromJson(
        {
          'environments': [],
          'test': {
            'cpu': 'x64',
            'label': '//scripts/lib:lib_tests(//build/toolchain:host_x64)',
            'name': 'lib_tests',
            'os': 'linux',
            'runtime_deps': 'host_x64/gen/scripts/lib/lib_tests.deps.json',
          }
        },
        buildDir: '/whatever',
      ));
      expect(await cmd.maybeAddPackageHash(bundle), true);
      expect(cmd.packageRepository, isNull);
      expect(bundle.testDefinition.hash, isNull);
    });

    test('not used when disabled', () async {
      var testsConfig = TestsConfig.fromRawArgs(
        rawArgs: ['--no-use-package-hash'],
        fxEnv: FakeFxEnv.shared,
      );
      var cmd = FuchsiaTestCommand.fromConfig(
        testsConfig,
        testRunnerBuilder: buildTestRunner,
      );
      final bundle = cmd.testBundleBuilder(TestDefinition.fromJson(
        {
          'environments': [],
          'test': {
            'cpu': 'x64',
            'label': '//src/sys/tests:test',
            'name': 'cmp_test',
            'os': 'fuchsia',
            'package_url':
                'fuchsia-pkg://fuchsia.com/cmp_test#meta/cmp_test.cm',
          }
        },
        buildDir: '/whatever',
      ));
      expect(await cmd.maybeAddPackageHash(bundle), true);
      expect(cmd.packageRepository, isNull);
      expect(bundle.testDefinition.hash, isNull);
    });

    test('used for component tests', () async {
      final fakeDir = await Directory.systemTemp.createTemp();
      final fxEnv = FakeFxEnv(fuchsiaDir: fakeDir.path);
      File('${fxEnv.outputDir}/package-repositories.json')
        ..createSync(recursive: true)
        ..writeAsStringSync(jsonEncode([
          {
            'targets': 'repo/targets.json',
            'blobs': 'repo/blobs.json',
            'path': 'repo'
          }
        ]));
      File('${fxEnv.outputDir}/repo/targets.json')
        ..createSync(recursive: true)
        ..writeAsStringSync(jsonEncode({
          'signed': {
            'targets': {
              'cmp_test/0': {
                'custom': {'merkle': '111111'}
              }
            }
          }
        }));
      var testsConfig = TestsConfig.fromRawArgs(
        rawArgs: [],
        fxEnv: fxEnv,
      );
      var cmd = FuchsiaTestCommand.fromConfig(
        testsConfig,
        testRunnerBuilder: buildTestRunner,
      );
      final bundle = cmd.testBundleBuilder(TestDefinition.fromJson(
        {
          'environments': [],
          'test': {
            'cpu': 'x64',
            'label': '//src/sys/tests:test',
            'name': 'cmp_test',
            'os': 'fuchsia',
            'package_url':
                'fuchsia-pkg://fuchsia.com/cmp_test#meta/cmp_test.cm',
          }
        },
        buildDir: '/whatever',
      ));
      expect(await cmd.maybeAddPackageHash(bundle), true);
      expect(cmd.packageRepository, isNotNull);
      expect(bundle.testDefinition.hash, '111111');
    });
  });
}
