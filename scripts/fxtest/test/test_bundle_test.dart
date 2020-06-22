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
        fx: '/whatever/fx',
      );
      final commandTokens = testDef.executionHandle.getInvocationTokens([]);
      expect(
        commandTokens.fullCommand,
        [
          '/whatever/fx',
          'shell',
          'run-test-component',
          '--restrict-logs',
          componentUrl
        ].join(' '),
      );
    });
  });
}
