// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:fxtest/fxtest.dart';
import 'package:path/path.dart' as p;

const String cwdToken = '.';

class FuchsiaLocator {
  static final FuchsiaLocator shared = FuchsiaLocator();
  final EnvReader envReader;
  FuchsiaLocator({envReader}) : envReader = envReader ?? EnvReader.shared;

  /// Absolute path to the build directory. Read from the environment variable.
  String get buildDir => envReader.getEnv('FUCHSIA_BUILD_DIR');

  /// Absolute path to the root of the Fuchsia checkout. Read from the
  /// environment variable.
  String get fuchsiaDir => envReader.getEnv('FUCHSIA_DIR');

  /// The current working directory. Pulled from the OS.
  String get cwd => envReader.getCwd();

  /// The current build directory relative to its Fuchsia tree.
  String get relativeBuildDir => buildDir?.substring(fuchsiaDir.length);

  /// The standard way of writing the build directory (e.g., "//out/default").
  String get userFriendlyBuildDir =>
      relativeBuildDir != buildDir ? '/$relativeBuildDir' : buildDir;

  /// The current working directory relative to the build directory.
  String get relativeCwd {
    // `cwd` asked for from the build root
    if (cwd == buildDir) {
      return cwdToken;
    }

    // `cwd` asked for from *within* the build directory
    if (cwd.startsWith(buildDir)) {
      String _path = cwd.substring(buildDir.length);

      // Remove a leading slash
      if (_path.startsWith(p.separator)) {
        _path = _path.substring(1);
      }

      return _path;
    }

    // All other situations, return the actual current directory
    return cwd;
  }
}
