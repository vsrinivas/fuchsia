// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TODO(https://fxbug.dev/84961): Fix null safety and remove this language version.
// @dart=2.9

import 'package:fxutils/fxutils.dart';
import 'package:test/test.dart';

void main() {
  group('FxEnv locates Fuchsia directories correctly', () {
    test('when the build directory is inside the checkout', () {
      var rootLocator = FxEnv.env({
        'FUCHSIA_DIR': '/root/path/fuchsia',
        'FUCHSIA_BUILD_DIR': '/root/path/fuchsia/out/default',
      });
      expect(rootLocator.fuchsiaDir, '/root/path/fuchsia');
      expect(rootLocator.outputDir, '/root/path/fuchsia/out/default');
      expect(rootLocator.relativeOutputDir, '/out/default');
      expect(rootLocator.userFriendlyOutputDir, '//out/default');
    });

    test('when the build directory is inside the checkout from `environment`',
        () {
      var rootLocator = FxEnv.env({
        'FUCHSIA_DIR': '/root/path/fuchsia',
        'FUCHSIA_BUILD_DIR': '/root/path/fuchsia/out/default',
      });
      expect(rootLocator.fuchsiaDir, '/root/path/fuchsia');
      expect(rootLocator.outputDir, '/root/path/fuchsia/out/default');
      expect(rootLocator.relativeOutputDir, '/out/default');
      expect(rootLocator.userFriendlyOutputDir, '//out/default');
    });

    test(
        'when the build directory is inside the checkout from `environment` '
        'and `overrides`', () {
      var envReader = EnvReader(
        cwd: '/cwd',
        environment: {
          'FUCHSIA_DIR': '/root/path/fuchsia',
          'FUCHSIA_BUILD_DIR': '/root/path/fuchsia/out/default',
        },
        overrides: {
          'FUCHSIA_DIR': '/custom/path',
          'FUCHSIA_BUILD_DIR': '/custom/path/out/default',
        },
      );
      var rootLocator = FxEnv(envReader: envReader);
      expect(rootLocator.fuchsiaDir, '/custom/path');
      expect(rootLocator.outputDir, '/custom/path/out/default');
      expect(rootLocator.relativeOutputDir, '/out/default');
      expect(rootLocator.userFriendlyOutputDir, '//out/default');
    });

    test('when the cwd is requested from the build dir itself', () {
      var envReader = EnvReader(
        cwd: '/root/path/fuchsia/out/default',
        environment: {
          'FUCHSIA_DIR': '/root/path/fuchsia',
          'FUCHSIA_BUILD_DIR': '/root/path/fuchsia/out/default',
        },
      );
      var rootLocator = FxEnv(envReader: envReader);
      expect(rootLocator.relativeCwd, '.');
    });

    test('when the cwd is requested from the build dir itself with `env`', () {
      var envReader = EnvReader(
        cwd: '/root/path/fuchsia/out/default',
        environment: {
          'FUCHSIA_DIR': '/root/path/fuchsia',
          'FUCHSIA_BUILD_DIR': '/root/path/fuchsia/out/default',
        },
      );
      var rootLocator = FxEnv(envReader: envReader);
      expect(rootLocator.relativeCwd, '.');
    });

    test('when the cwd is requested from within the build dir', () {
      var envReader = EnvReader(
        cwd: '/root/path/fuchsia/out/default/host_x64',
        environment: {
          'FUCHSIA_DIR': '/root/path/fuchsia',
          'FUCHSIA_BUILD_DIR': '/root/path/fuchsia/out/default',
        },
      );
      var rootLocator = FxEnv(envReader: envReader);
      expect(rootLocator.relativeCwd, 'host_x64');
    });

    test(
        'when the cwd is requested from within the tree but not within '
        'the build dir', () {
      var envReader = EnvReader(
        cwd: '/root/path/fuchsia/tools',
        environment: {
          'FUCHSIA_DIR': '/root/path/fuchsia',
          'FUCHSIA_BUILD_DIR': '/root/path/fuchsia/out/default',
        },
      );
      var rootLocator = FxEnv(envReader: envReader);
      expect(rootLocator.relativeCwd, '/root/path/fuchsia/tools');
    });

    test('when fuchsia tree directory path does not contain "fuchsia"', () {
      var envReader = EnvReader(
        cwd: '/root/path/dev/out/default',
        environment: {
          'FUCHSIA_DIR': '/root/path/fuchsia',
          'FUCHSIA_BUILD_DIR': '/root/path/dev/out/default',
        },
      );
      var rootLocator = FxEnv(envReader: envReader);
      expect(rootLocator.relativeCwd, '.');
    });
  });

  group('FxEnv', () {
    test('should return specific env variables when they are passed', () {
      final fxEnv = FxEnv.env({'FUCHSIA_DIR': '/some/path'});
      expect(fxEnv.fuchsiaDir, equals('/some/path'));
    });
  });
}
