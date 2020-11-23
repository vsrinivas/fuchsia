// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:fxutils/fxutils.dart';
import 'package:path/path.dart' as p;

abstract class IFxEnv {
  String? getEnv(String variableName, [String? defaultValue]);

  /// Path to the fx executable.
  String get fx => p.join(fuchsiaDir!, fxLocation);
  String? get sshKey;
  String? get outputDir;
  String? get fuchsiaDir;
  String? get fuchsiaArch;
  String? get hostOutDir;
  String? get zirconBuildRoot;
  String? get zirconToolsDir;
  String get cwd;

  /// Relative path to the current output directory from the root of the Fuchsia
  /// checkout.
  String? get relativeOutputDir => outputDir?.substring(fuchsiaDir!.length);

  /// GN absolute path to the output directory (e.g., "//out/default").
  String? get userFriendlyOutputDir =>
      relativeOutputDir != outputDir ? '/$relativeOutputDir' : outputDir;

  /// Relative path between the current working directory and the output
  /// directory.
  String get relativeCwd {
    // `startingDir` asked for from the output root
    if (cwd == outputDir) {
      return cwdToken;
    }

    // `cwd` asked for from *within* the output directory
    if (p.isWithin(outputDir!, cwd)) {
      return p.relative(cwd, from: outputDir);
    }

    // All other situations, return the actual starting directory.
    return cwd;
  }
}

/// Usage:
/// ```dart
/// final fxEnv = FxEnv.env(Platform.environment);
///
/// // Get the absolute path to the fx executable
/// String fxPath = fxEnv.fx;
///
/// // Get the path to the directory of your current active build
/// String buildPath = fxEnv.outputDir;
/// ```
class FxEnv extends IFxEnv {
  final EnvReader _envReader;
  FxEnv({required EnvReader envReader}) : _envReader = envReader;

  factory FxEnv.env(Map<String, String> env, {String cwd = '/cwd'}) =>
      FxEnv(envReader: EnvReader(environment: env, cwd: cwd));

  @override
  String? getEnv(String variableName, [String? defaultValue]) =>
      _envReader.getEnv(variableName, defaultValue);

  /// Path to the ssh key required to reach the device.
  @override
  String? get sshKey => _envReader.getEnv('FUCHSIA_SSH_KEY');

  /// Absolute path to the build directory. Read from the environment variable.
  @override
  String? get outputDir => _envReader.getEnv('FUCHSIA_BUILD_DIR');

  /// Absolute path to the root of the Fuchsia checkout. Read from the
  /// environment variable.
  @override
  String? get fuchsiaDir => _envReader.getEnv('FUCHSIA_DIR');

  /// The current architecture selected (currently one of x64/arm64)
  @override
  String? get fuchsiaArch => _envReader.getEnv('FUCHSIA_ARCH');

  /// The path to the Fuchsia host-tools build directory
  /// (usually $FUCHSIA_BUILD_DIR/host_$HOST_ARCH)
  @override
  String? get hostOutDir => _envReader.getEnv('HOST_OUT_DIR');

  @override
  String? get zirconBuildRoot => _envReader.getEnv('ZIRCON_BUILDROOT');

  @override
  String? get zirconToolsDir => _envReader.getEnv('ZIRCON_TOOLS_DIR');

  /// Current working directory. Pulled from the OS.
  @override
  String get cwd => _envReader.getCwd();
}
