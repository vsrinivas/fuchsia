// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:io';

/// Wraps [Platform] for the sake of mocking env results in tests.
///
/// For now, this class only has a pass-thru to [Platform.environment]
/// since that's all that's currently needed, but in theory it may be nice to
/// use this to test all sorts of platform-related checks we might make.
///
/// Note that, for now, this is unnecessary if there is actually a way to mock
/// environment variables on a per-test basis, but after some research, I am
/// unaware of any such trick.
///
/// Usage:
/// ```dart
/// final envReader = EnvReader(environment: Platform.environment);
/// String envVariableValue = envReader.getEnv('MY_ENV_VAR');
/// ```
class EnvReader {
  final String cwd;

  final Map<String, String> environment;
  final Map<String, String> overrides;

  EnvReader({
    required this.environment,
    required this.cwd,
    this.overrides = const {},
  });
  factory EnvReader.fromEnvironment() =>
      EnvReader(environment: Platform.environment, cwd: Directory.current.path);

  String? getEnv(String variableName, [String? defaultValue]) {
    // print('asking for $variableName with overrides: $overrides');
    // print('asking for $variableName with environment: $environment');
    return overrides[variableName] ?? environment[variableName] ?? defaultValue;
  }

  String getCwd() => overrides['cwd'] ?? cwd;
}
