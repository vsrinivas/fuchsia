// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:fxtest/fxtest.dart';
import 'package:path/path.dart' as p;

/// A filter on compatibility between various `fx test ...` invocations
/// and flavors of desired behavior.
abstract class Checker {
  /// Provides compatibility checks for given `fx test` parameters. Returns
  /// `true` if `testsConfig` is compatible with `testDefinition`.
  ///
  /// Separates the two logical chunks of whether a single test is aligned with
  /// a user's test run parameters. Importantly, this allows us to compose
  /// Checkers out of Mixins and avoid requiring a combinatorial amount of
  /// Checker subclasses to solve the possibly combinatorial amount of
  /// situations.
  bool canHandle(
      PermutatedTestsConfig testsConfig, TestDefinition testDefinition) {
    return _testPassesConfig(testsConfig, testDefinition) &&
        _testPassesNameCheck(testsConfig.testName, testDefinition);
  }

  /// Compares a [TestDefinition] against the complete set of flags provided by
  /// the developer.
  ///
  /// Compatibility filter that is called once per test, since the flags are
  /// considered as a whole and are not permutated like `testName` values.
  /// For example, a developer may pass `--host` to signify that they only want
  /// to run host tests, or possibly `--host -XYZ` to append the `XYZ` filter.
  /// In either case, a test either survives all flag checks or not.
  bool _testPassesConfig(
    PermutatedTestsConfig testsConfig,
    TestDefinition testDefinition,
  ) {
    if (testsConfig.flags.shouldOnlyRunDeviceTests &&
        testDefinition.os != 'fuchsia') {
      return false;
    }
    if (testsConfig.flags.shouldOnlyRunHostTests &&
        testDefinition.os == 'fuchsia') {
      return false;
    }
    return true;
  }

  /// Compares a [TestDefinition] against a single `testName` parameter provided
  /// by the developer.
  ///
  /// Compatibility filter that is called N times for each test, where N is the
  /// number of `testNames` provided by the user. In a situation where the
  ///  developer supplied `["//networking", "//graphics"]` for `testNames`, this
  /// function will be called twice (once for each value) for each test.
  bool _testPassesNameCheck(String testName, TestDefinition testDefinition);
}

class ComponentTestChecker extends Checker {
  /// Returns `true` if the [TestDefinition] from `tests.json` both starts
  /// with "//" and starts with our `testName` value.
  @override
  bool _testPassesNameCheck(String testName, TestDefinition testDefinition) {
    return testName != null &&
        testName.startsWith('//') &&
        (testDefinition.name.startsWith(testName) ||
            testDefinition.label.startsWith(testName));
  }
}

class NameMatchChecker extends Checker {
  @override
  bool _testPassesNameCheck(String testName, TestDefinition testDefinition) {
    return testName.toLowerCase() == testDefinition.name.toLowerCase();
  }
}

class FullUrlComponentChecker extends Checker {
  static const _fuchsiaPkgPrefix = 'fuchsia-pkg://';

  /// Returns `true` if passed `testName` is both a validated Fuchsia Package
  /// URL and matches a [TestDefinition] from `tests.json`
  @override
  bool _testPassesNameCheck(String testName, TestDefinition testDefinition) {
    return testName != null &&
        testName.startsWith(FullUrlComponentChecker._fuchsiaPkgPrefix) &&
        testDefinition.packageUrl != null &&
        testDefinition.packageUrl.startsWith(testName);
  }
}

class UrlNameComponentChecker extends Checker {
  /// Returns `true` if passed `testName` matches the `packageName` chunk of
  /// this [TestDefinition] instance's [PackageUrl] object
  @override
  bool _testPassesNameCheck(String testName, TestDefinition testDefinition) {
    return testName != null && testName == testDefinition.parsedUrl.packageName;
  }
}

/// Checker that green-lights every test if a user passed no test names, but
/// is a no-op otherwise.
class NoArgumentsChecker extends Checker {
  /// Returns [true] if the supplied `testName` is `null`. AKA, if the developer
  /// executed `fx test` with no positional arguments (but possibly some flags).
  @override
  bool _testPassesNameCheck(String testName, TestDefinition testDefinition) =>
      testName == null;
}

/// Checker that green-lights a test that matches or descends from a supplied
/// path in the build output.
class PathMatchChecker extends Checker {
  @override
  bool _testPassesNameCheck(String testName, TestDefinition testDefinition) {
    if (testDefinition.path == null) return false;

    // A dot here signifies that the user ran `fx test .` *from the build
    // directory itself*, which means that all host test paths, which are
    // relative from that directory, must obviously pass.
    // Related to this is that host test paths are written as relative paths,
    // (e.g., no leading slash) and on-device tests are written as absolute
    // paths (often starting with "/pkgfs").
    // So in closing, here we want to match all paths that are relative (and
    // by extension, host tests nested inside the build directory).
    if (testName == '.' && !testDefinition.path.startsWith(p.separator)) {
      return true;
    }

    // Otherwise, do the standard
    return testDefinition.path.startsWith(testName);
  }
}
