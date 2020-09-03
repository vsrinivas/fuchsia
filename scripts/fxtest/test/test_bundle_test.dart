// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:test/test.dart';
import 'package:fxtest/fxtest.dart';

void main() {
  group('TestBundle', () {
    test('should assemble correct arguments for component tests', () {
      const componentUrl =
          'fuchsia-pkg://fuchsia.com/pkg-name#meta/component-name.cmx';
      final testDef = TestDefinition.fromJson(
        {
          'environments': [],
          'test': {
            'cpu': 'x64',
            'label': '//scripts/lib:lib_tests(//build/toolchain:host_x64)',
            'name': 'lib_tests',
            'os': 'fuchsia',
            'package_url': componentUrl,
            'runtime_deps': 'host_x64/gen/scripts/lib/lib_tests.deps.json'
          }
        },
        buildDir: '/whatever',
      );
      final commandTokens =
          testDef.createExecutionHandle().getInvocationTokens([]);
      expect(
        commandTokens.fullCommand,
        ['fx', 'shell', 'run-test-component', componentUrl].join(' '),
      );
      final commandTokens2 = testDef
          .createExecutionHandle()
          .getInvocationTokens(['--max-log-severity=WARN']);
      expect(
        commandTokens2.fullCommand,
        [
          'fx',
          'shell',
          'run-test-component',
          '--max-log-severity=WARN',
          componentUrl
        ].join(' '),
      );
    });

    test('should assemble correct arguments for component suite tests', () {
      const componentUrl =
          'fuchsia-pkg://fuchsia.com/pkg-name#meta/component-name.cm';
      final testDef = TestDefinition.fromJson(
        {
          'environments': [],
          'test': {
            'cpu': 'x64',
            'label': '//scripts/lib:lib_tests(//build/toolchain:host_x64)',
            'name': 'lib_tests',
            'os': 'fuchsia',
            'package_url': componentUrl,
            'runtime_deps': 'host_x64/gen/scripts/lib/lib_tests.deps.json',
            'parallel': 2,
          }
        },
        buildDir: '/whatever',
      );
      final commandTokens =
          testDef.createExecutionHandle().getInvocationTokens([]);
      expect(
        commandTokens.fullCommand,
        ['fx', 'shell', 'run-test-suite', '--parallel 2', componentUrl]
            .join(' '),
      );

      final testDef2 = TestDefinition.fromJson(
        {
          'environments': [],
          'test': {
            'cpu': 'x64',
            'label': '//scripts/lib:lib_tests(//build/toolchain:host_x64)',
            'name': 'lib_tests',
            'os': 'fuchsia',
            'package_url': componentUrl,
            'runtime_deps': 'host_x64/gen/scripts/lib/lib_tests.deps.json',
          }
        },
        buildDir: '/whatever',
      );

      // passed flag in should be ignored.
      final commandTokens2 =
          testDef2.createExecutionHandle().getInvocationTokens(['--some-flag']);
      expect(
        commandTokens2.fullCommand,
        ['fx', 'shell', 'run-test-suite', componentUrl].join(' '),
      );
    });
  });
}
