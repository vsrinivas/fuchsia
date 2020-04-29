// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:fxtest/fxtest.dart';
import 'package:path/path.dart' as p;

/// A filter on compatibility between various `fx test ...` invocations
/// and flavors of desired behavior.
abstract class Checker {
  /// List of [MatchType]s a certain [Checker] is allowed to evaluate. Do not
  /// bother adding [MatchType.unrestricted], as that is always allowed to pass.
  List<MatchType> allowedMatchTypes = const [];

  /// Provides compatibility checks for given `fx test` parameters. Returns
  /// `true` if `testsConfig` is compatible with `testDefinition`.
  ///
  /// Separates the two logical chunks of whether a single test is aligned with
  /// a user's test run parameters. Importantly, this allows us to compose
  /// Checkers out of Mixins and avoid requiring a combinatorial amount of
  /// Checker subclasses to solve the possibly combinatorial amount of
  /// situations.
  bool canHandle(String testName, MatchType matchType, Flags flags,
      TestDefinition testDefinition,
      {bool exactMatching}) {
    return _testPassesFlags(flags, testDefinition) &&
        _matchTypeIsAllowed(matchType) &&
        _testPassesNullAwareNameCheck(testName, testDefinition,
            exactMatching: exactMatching);
  }

  bool _matchTypeIsAllowed(MatchType matchType) =>
      matchType == MatchType.unrestricted ||
      allowedMatchTypes.contains(matchType);

  /// Compares a [TestDefinition] against the complete set of flags provided by
  /// the developer.
  ///
  /// Compatibility filter that is called once per test, since the flags are
  /// considered as a whole and are not permutated like `testName` values.
  /// For example, a developer may pass `--host` to signify that they only want
  /// to run host tests, or possibly `--host -XYZ` to append the `XYZ` filter.
  /// In either case, a test either survives all flag checks or not.
  bool _testPassesFlags(Flags flags, TestDefinition testDefinition) {
    if (flags.shouldOnlyRunDeviceTests && testDefinition.os != 'fuchsia') {
      return false;
    }
    if (flags.shouldOnlyRunHostTests && testDefinition.os == 'fuchsia') {
      return false;
    }
    return true;
  }

  /// Wrapper around [_testPassesNameCheck] which handles the case where the
  /// [testName] is [null], because that logic is straightforward and this way,
  /// no other implementations have to worry about [null] values.
  bool _testPassesNullAwareNameCheck(
    String testName,
    TestDefinition testDefinition, {
    bool exactMatching,
  }) {
    return testName == null
        ? this is NoArgumentsChecker
        : _testPassesNameCheck(testName, testDefinition,
            exactMatching: exactMatching);
  }

  /// Compares a [TestDefinition] against a single `testName` parameter provided
  /// by the developer.
  ///
  /// Compatibility filter that is called N times for each test, where N is the
  /// number of `testNames` provided by the user. In a situation where the
  ///  developer supplied `["//networking", "//graphics"]` for `testNames`, this
  /// function will be called twice (once for each value) for each test.
  bool _testPassesNameCheck(String testName, TestDefinition testDefinition,
      {bool exactMatching});
}

class LabelChecker extends Checker {
  /// Returns `true` if the [TestDefinition]'s `"label"` field starts with
  /// the given [testName].
  @override
  bool _testPassesNameCheck(String testName, TestDefinition testDefinition,
      {bool exactMatching}) {
    if (testDefinition.label == null) return false;
    return exactMatching
        ? testName == testDefinition.label
        : testDefinition.label.startsWith(testName);
  }
}

class NameChecker extends Checker {
  @override
  bool _testPassesNameCheck(String testName, TestDefinition testDefinition,
      {bool exactMatching}) {
    if (testDefinition.name == null) return false;
    return testName.toLowerCase() == testDefinition.name.toLowerCase();
  }
}

class PackageUrlChecker extends Checker {
  /// Returns `true` if passed `testName` is both a validated Fuchsia Package
  /// URL and matches a [TestDefinition] from `tests.json`
  @override
  bool _testPassesNameCheck(String testName, TestDefinition testDefinition,
      {bool exactMatching}) {
    if (exactMatching) {
      return testName == testDefinition.packageUrl;
    }
    return testDefinition.packageUrl != null &&
        testDefinition.packageUrl.startsWith(testName);
  }
}

class ComponentNameChecker extends Checker {
  @override
  List<MatchType> get allowedMatchTypes => [MatchType.componentName];

  /// Returns `true` if passed `testName` matches the `componentName` chunk of
  /// this [TestDefinition] instance's [PackageUrl] object
  @override
  bool _testPassesNameCheck(String testName, TestDefinition testDefinition,
      {bool exactMatching}) {
    return (testName == testDefinition.parsedUrl?.fullComponentName ||
        testName == testDefinition.parsedUrl?.componentName);
  }
}

class PackageNameChecker extends Checker {
  @override
  List<MatchType> get allowedMatchTypes => [MatchType.packageName];

  /// Returns `true` if passed `testName` matches the `packageName` chunk of
  /// this [TestDefinition] instance's [PackageUrl] object
  @override
  bool _testPassesNameCheck(String testName, TestDefinition testDefinition,
      {bool exactMatching}) {
    return testName == testDefinition.parsedUrl?.packageName;
  }
}

/// Checker that green-lights every test if a user passed no test names, but
/// is a no-op otherwise.
class NoArgumentsChecker extends Checker {
  // Never called when the value is actually `null`, due to the structure of
  // `Checker`, so must always return `false`.
  @override
  bool _testPassesNameCheck(
    String testName,
    TestDefinition testDefinition, {
    bool exactMatching,
  }) =>
      false;
}

/// Checker that green-lights a test that matches or descends from a supplied
/// path in the build output.
class PathMatchChecker extends Checker {
  @override
  bool _testPassesNameCheck(String testName, TestDefinition testDefinition,
      {bool exactMatching}) {
    if (testDefinition.path == null) return false;
    if (exactMatching) return testName == testDefinition.path;

    // A dot here signifies that the user ran `fx test .` *from the build
    // directory itself*, which means that all host test paths which are
    // relative from that directory must obviously pass.
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
