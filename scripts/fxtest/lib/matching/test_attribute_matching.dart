// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TODO(https://fxbug.dev/84961): Fix null safety and remove this language version.
// @dart=2.9

import 'package:fxtest/fxtest.dart';
import 'package:meta/meta.dart';
import 'package:path/path.dart' as p;

enum MatchLength {
  /// Matching mode that requires entire matches. Does not allow contains,
  /// startsWith, or endsWith.
  full,

  /// Matching mode that allows for partial matches. DOES allow contains,
  /// startsWith, or endsWith.
  partial,
}

/// A filter on compatibility between various `fx test ...` invocations
/// and flavors of desired behavior.
abstract class TestAttributeMatcher {
  /// List of [MatchType]s a certain [TestAttributeMatcher] is allowed to evaluate.
  /// Do not bother adding [MatchType.unrestricted], as that is always allowed
  /// to pass.
  List<MatchType> allowedMatchTypes = const [];

  /// Provides compatibility checks for given `fx test` parameters. Returns
  /// `true` if `testsConfig` is compatible with `testDefinition`.
  ///
  /// Separates the two logical chunks of whether a single test is aligned with
  /// a user's test run parameters. Importantly, this allows us to compose
  /// [TestAttributeMatchers] out of Mixins and avoid requiring a combinatorial
  /// amount of [TestAttributeMatcher] subclasses to solve the possibly combinatorial
  /// amount of situations.
  ComparisonResult isAttributeMatch(
    String testName,
    TestDefinition testDefinition, {
    @required MatchType matchType,
    @required Flags flags,
    @required Comparer comparer,
    @required MatchLength matchLength,
  }) {
    bool isAllowed = _testPassesFlags(flags, testDefinition) &&
        _matchTypeIsAllowed(matchType);
    if (isAllowed) {
      return _testPassesNullAwareNameCheck(
        testName,
        testDefinition,
        matchLength: matchLength,
        comparer: comparer,
      );
    }
    return ComparisonResult.failure;
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
  ComparisonResult _testPassesNullAwareNameCheck(
    String testName,
    TestDefinition testDefinition, {
    MatchLength matchLength,
    Comparer comparer,
  }) {
    // Handles the case where a user typed `fx test` without any test names
    // (but possible flags).
    if (testName == null) {
      return ComparisonResult.strict(isMatch: this is NoArgumentsMatcher);
    }
    // Handles cases where a user typed `fx test [test-name...]` (and possible
    // flags).
    return _testPassesNameCheck(testName, testDefinition,
        matchLength: matchLength, comparer: comparer);
  }

  /// Compares a [TestDefinition] against a single `testName` parameter provided
  /// by the developer.
  ///
  /// Compatibility filter that is called N times for each test, where N is the
  /// number of `testNames` provided by the user. In a situation where the
  ///  developer supplied `["//networking", "//graphics"]` for `testNames`, this
  /// function will be called twice (once for each value) for each test.
  ComparisonResult _testPassesNameCheck(
      String testName, TestDefinition testDefinition,
      {MatchLength matchLength, Comparer comparer});
}

class LabelMatcher extends TestAttributeMatcher {
  /// Indicates a positive match if the [TestDefinition]'s `"label"` field matches
  /// the given [testName].
  @override
  ComparisonResult _testPassesNameCheck(
      String testName, TestDefinition testDefinition,
      {MatchLength matchLength, Comparer comparer}) {
    if (testDefinition.label == null) return ComparisonResult.failure;
    return matchLength == MatchLength.full
        ? comparer.equals(testDefinition.label, testName)
        : comparer.startsWith(testDefinition.label, testName);
  }
}

class RuntimeDepsMatcher extends TestAttributeMatcher {
  /// Indicates a positive match if the [TestDefinition]'s `"runtime_deps"` field
  /// matches the given [testName].
  @override
  ComparisonResult _testPassesNameCheck(
      String testName, TestDefinition testDefinition,
      {MatchLength matchLength, Comparer comparer}) {
    if (testDefinition.runtimeDeps == null) return ComparisonResult.failure;
    return matchLength == MatchLength.full
        ? comparer.equals(testName, testDefinition.runtimeDeps)
        : comparer.contains(testDefinition.runtimeDeps, testName);
  }
}

class NameMatcher extends TestAttributeMatcher {
  @override
  ComparisonResult _testPassesNameCheck(
      String testName, TestDefinition testDefinition,
      {MatchLength matchLength, Comparer comparer}) {
    if (testDefinition.name == null) return ComparisonResult.failure;
    return matchLength == MatchLength.full
        ? comparer.equals(testName, testDefinition.name)
        : comparer.contains(testName, testDefinition.name);
  }
}

class PackageUrlMatcher extends TestAttributeMatcher {
  /// Indicates a positive match if the passed `testName` is both a validated
  /// Fuchsia Package URL and matches a [TestDefinition] from `tests.json`
  @override
  ComparisonResult _testPassesNameCheck(
      String testName, TestDefinition testDefinition,
      {MatchLength matchLength, Comparer comparer}) {
    if (testDefinition.packageUrl == null) return ComparisonResult.failure;
    return matchLength == MatchLength.full
        ? comparer.equals(testDefinition.packageUrl.toString(), testName)
        : comparer.startsWith(testDefinition.packageUrl.toString(), testName);
  }
}

class ComponentNameMatcher extends TestAttributeMatcher {
  @override
  List<MatchType> get allowedMatchTypes => [MatchType.componentName];

  /// Indicates a positive match if the passed `testName` matches the
  /// `componentName` chunk of this [TestDefinition] instance's [PackageUrl]
  /// object.
  @override
  ComparisonResult _testPassesNameCheck(
      String testName, TestDefinition testDefinition,
      {MatchLength matchLength, Comparer comparer}) {
    var fullCompResult =
        comparer.equals(testName, testDefinition.packageUrl?.fullComponentName);
    var partialCompResult =
        comparer.equals(testName, testDefinition.packageUrl?.componentName);
    return ComparisonResult.bestResult(fullCompResult, partialCompResult);
  }
}

class PackageNameMatcher extends TestAttributeMatcher {
  @override
  List<MatchType> get allowedMatchTypes => [MatchType.packageName];

  /// Indicates a positive match if passed `testName` matches the `packageName`
  /// chunk of this [TestDefinition] instance's [PackageUrl] object.
  @override
  ComparisonResult _testPassesNameCheck(
      String testName, TestDefinition testDefinition,
      {MatchLength matchLength, Comparer comparer}) {
    return comparer.equals(testName, testDefinition.packageUrl?.packageName);
  }
}

/// [TestAttributeMatcher] that green-lights every test if a user passed no test
/// names, but is a no-op otherwise.
class NoArgumentsMatcher extends TestAttributeMatcher {
  // Never called when the value is actually `null`, due to the structure of
  // `TestAttributeMatcher`, so must always return `false`.
  @override
  ComparisonResult _testPassesNameCheck(
    String testName,
    TestDefinition testDefinition, {
    MatchLength matchLength,
    Comparer comparer,
  }) =>
      ComparisonResult.failure;
}

/// [TestAttributeMatcher] that green-lights a test that matches or descends from
/// a supplied path in the build output.
class PathMatcher extends TestAttributeMatcher {
  @override
  ComparisonResult _testPassesNameCheck(
      String testName, TestDefinition testDefinition,
      {MatchLength matchLength, Comparer comparer}) {
    if (testDefinition.path == null) return ComparisonResult.failure;
    if (matchLength == MatchLength.full) {
      return comparer.equals(testName, testDefinition.path);
    }
    // A dot here signifies that the user ran `fx test .` *from the build
    // directory itself*, which means that all host test paths which are
    // relative from that directory must obviously pass.
    // Related to this is that host test paths are written as relative paths,
    // (e.g., no leading slash) and on-device tests are written as absolute
    // paths (often starting with "/pkgfs").
    // Here we want to match all paths that are relative (and by extension, host
    // tests nested inside the build directory). This type of clause also makes
    // no sense to engage with FuzzyMatching, so we skip over it for that phase.
    if (testName == '.' &&
        !testDefinition.path.startsWith(p.separator) &&
        comparer is StrictComparer) {
      return ComparisonResult.withConfidence(1);
    }
    // Otherwise, do the standard
    return comparer.contains(testDefinition.path, testName);
  }
}
