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
class EnvReader {
  static Map<String, String> environment = Platform.environment;
  static String cwd = Directory.current.path;
  Map<String, String> overrides;

  EnvReader({this.overrides = const {}});

  String getEnv(String variableName, [String defaultValue]) =>
      overrides[variableName] ?? environment[variableName] ?? defaultValue;

  String getCwd() => overrides['cwd'] ?? cwd;
}
