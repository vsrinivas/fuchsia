// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:fxutils/fxutils.dart';
import 'package:test/test.dart';

void main() {
  group('fuchsia directories are located correctly', () {
    test('when the build directory is inside the checkout', () {
      var envReader = EnvReader(overrides: {
        'FUCHSIA_DIR': '/root/path/fuchsia',
        'FUCHSIA_BUILD_DIR': '/root/path/fuchsia/out/default',
      });
      var rootLocator = FxEnv(envReader: envReader);
      expect(rootLocator.fuchsiaDir, '/root/path/fuchsia');
      expect(rootLocator.outputDir, '/root/path/fuchsia/out/default');
      expect(rootLocator.relativeOutputDir, '/out/default');
      expect(rootLocator.userFriendlyOutputDir, '//out/default');
    });

    test('when the cwd is requested from the build dir itself', () {
      var envReader = EnvReader(overrides: {
        'cwd': '/root/path/fuchsia/out/default',
        'FUCHSIA_DIR': '/root/path/fuchsia',
        'FUCHSIA_BUILD_DIR': '/root/path/fuchsia/out/default',
      });
      var rootLocator = FxEnv(envReader: envReader);
      expect(rootLocator.relativeCwd, '.');
    });

    test('when the cwd is requested from within the build dir', () {
      var envReader = EnvReader(overrides: {
        'cwd': '/root/path/fuchsia/out/default/host_x64',
        'FUCHSIA_DIR': '/root/path/fuchsia',
        'FUCHSIA_BUILD_DIR': '/root/path/fuchsia/out/default',
      });
      var rootLocator = FxEnv(envReader: envReader);
      expect(rootLocator.relativeCwd, 'host_x64');
    });

    test(
        'when the cwd is requested from within the tree but not within '
        'the build dir', () {
      var envReader = EnvReader(overrides: {
        'cwd': '/root/path/fuchsia/tools',
        'FUCHSIA_DIR': '/root/path/fuchsia',
        'FUCHSIA_BUILD_DIR': '/root/path/fuchsia/out/default',
      });
      var rootLocator = FxEnv(envReader: envReader);
      expect(rootLocator.relativeCwd, '/root/path/fuchsia/tools');
    });

    test('when fuchsia tree directory path does not contain "fuchsia"', () {
      var envReader = EnvReader(overrides: {
        'cwd': '/root/path/dev/out/default',
        'FUCHSIA_DIR': '/root/path/fuchsia',
        'FUCHSIA_BUILD_DIR': '/root/path/dev/out/default',
      });
      var rootLocator = FxEnv(envReader: envReader);
      expect(rootLocator.relativeCwd, '.');
    });
  });
}
