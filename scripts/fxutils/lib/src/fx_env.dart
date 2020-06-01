// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:path/path.dart' as p;
import 'package:fxutils/fxutils.dart';

class FxEnv {
  final EnvReader _envReader;
  FxEnv({EnvReader envReader}) : _envReader = envReader ?? EnvReader();

  /// Path to the fx executable
  String get fx => p.join(fuchsiaDir, fxLocation);

  /// Absolute path to the build directory. Read from the environment variable.
  String get outputDir => _envReader.getEnv('FUCHSIA_BUILD_DIR');

  /// Absolute path to the root of the Fuchsia checkout. Read from the
  /// environment variable.
  String get fuchsiaDir => _envReader.getEnv('FUCHSIA_DIR');

  /// The current architecture selected (currently one of x64/arm64)
  String get fuchsiaArch => _envReader.getEnv('FUCHSIA_ARCH');

  /// The path to the Fuchsia host-tools build directory
  /// (usually $FUCHSIA_BUILD_DIR/host_$HOST_ARCH)
  String get hostOutDir => _envReader.getEnv('HOST_OUT_DIR');

  String get zirconBuildRoot => _envReader.getEnv('ZIRCON_BUILDROOT');

  String get zirconToolsDir => _envReader.getEnv('ZIRCON_TOOLS_DIR');

  /// Current working directory. Pulled from the OS.
  String get cwd => _envReader.getCwd();

  /// Relative path to the current output directory from the root of the Fuchsia
  /// checkout.
  String get relativeOutputDir => outputDir?.substring(fuchsiaDir.length);

  /// GN absolute path to the output directory (e.g., "//out/default").
  String get userFriendlyOutputDir =>
      relativeOutputDir != outputDir ? '/$relativeOutputDir' : outputDir;

  /// The current working directory relative to the output directory.
  String get relativeCwd {
    // `cwd` asked for from the output root
    if (cwd == outputDir) {
      return cwdToken;
    }

    // `cwd` asked for from *within* the output directory
    if (p.isWithin(outputDir, cwd)) {
      return p.relative(cwd, from: outputDir);
    }

    // All other situations, return the actual current directory
    return cwd;
  }
}
