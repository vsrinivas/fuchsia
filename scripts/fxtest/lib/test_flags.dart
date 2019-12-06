// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:meta/meta.dart';

/// Simple class to hold shared parameters.
class _FlagsMixin {
  final bool dryRun;
  final bool isVerbose;

  /// The maximum number of tests to run. If 0, all tests will be executed.
  final int limit;
  final bool allOutput;
  final bool simpleOutput;
  final bool shouldOnlyRunDeviceTests;
  final bool shouldOnlyRunHostTests;
  final bool shouldFailFast;
  final bool shouldPrintSkipped;
  final bool shouldRandomizeTestOrder;
  final bool shouldSilenceUnsupported;
  final int warnSlowerThan;
  _FlagsMixin({
    this.dryRun = false,
    this.isVerbose = false,
    this.limit = 0,
    this.allOutput = false,
    this.simpleOutput = false,
    this.shouldOnlyRunDeviceTests = false,
    this.shouldOnlyRunHostTests = false,
    this.shouldFailFast = false,
    this.shouldPrintSkipped = false,
    this.shouldRandomizeTestOrder = false,
    this.shouldSilenceUnsupported = false,
    this.warnSlowerThan = 0,
  });

  @override
  String toString() => '''<TestFlags
  dryRun: $dryRun
  allOutput: $allOutput,
  limit: $limit
  isVerbose: $isVerbose
  simpleOutput: $simpleOutput,
  shouldOnlyRunDeviceTests: $shouldOnlyRunDeviceTests
  shouldOnlyRunHostTests: $shouldOnlyRunHostTests
  shouldFailFast: $shouldFailFast
  shouldPrintSkipped: $shouldPrintSkipped
  shouldRandomizeTestOrder: $shouldRandomizeTestOrder
  shouldSilenceUnsupported: $shouldSilenceUnsupported
  warnSlowerThan: $warnSlowerThan
>''';
}

/// The parsed parameters passed by our test-running user for evaluation
/// against specific tests available to the current build.
///
/// This handles the fact that users can invoke flags we need to honor in a
/// combinatorial sort of way. For example, consider this invocation:
///
/// ```sh
/// fx test //network //bootloader -d
/// ```
///
/// Here, our developer wants to run all device network tests and all device
/// bootloader tests. To streamline the code that delivers this, we will expand
/// parameters and pretend the developer executed the following two commands:
///
/// ```sh
/// fx test //network -d
/// fx test //bootloader -d
/// ```
/// Each "imagined" command will produce a set of tests which will be combined
/// into a master list of tests we run in one go, with aggregated output.
/// However, the intermediate layer streamlines much of our matching logic.
///
/// [TestFlags] executes this combinatorial explosion by expanding its
/// parameters into a list of [PermutatedTestFlag] instances.
class TestFlags extends _FlagsMixin {
  final List<String> testNames;
  TestFlags({
    @required this.testNames,
    dryRun,
    isVerbose,
    limit,
    allOutput = false,
    simpleOutput = false,
    shouldFailFast = false,
    shouldOnlyRunDeviceTests = false,
    shouldOnlyRunHostTests = false,
    shouldPrintSkipped = false,
    shouldRandomizeTestOrder = false,
    shouldSilenceUnsupported = false,
    warnSlowerThan,
  }) : super(
          dryRun: dryRun,
          isVerbose: isVerbose,
          limit: limit,
          allOutput: allOutput,
          simpleOutput: simpleOutput,
          shouldFailFast: shouldFailFast,
          shouldOnlyRunDeviceTests: shouldOnlyRunDeviceTests,
          shouldOnlyRunHostTests: shouldOnlyRunHostTests,
          shouldPrintSkipped: shouldPrintSkipped,
          shouldRandomizeTestOrder: shouldRandomizeTestOrder,
          shouldSilenceUnsupported: shouldSilenceUnsupported,
          warnSlowerThan: warnSlowerThan,
        );

  factory TestFlags.all([List<String> tNames]) {
    return TestFlags(
      testNames: tNames ?? [],
      dryRun: false,
      limit: 0,
      isVerbose: false,
      allOutput: false,
      shouldOnlyRunDeviceTests: false,
      shouldOnlyRunHostTests: false,
      shouldFailFast: false,
      shouldPrintSkipped: false,
      shouldRandomizeTestOrder: false,
      shouldSilenceUnsupported: false,
      warnSlowerThan: 0,
    );
  }

  factory TestFlags.host(List<String> tNames) {
    return TestFlags(
      testNames: tNames,
      dryRun: false,
      limit: 0,
      isVerbose: false,
      allOutput: false,
      simpleOutput: true,
      shouldOnlyRunDeviceTests: false,
      shouldOnlyRunHostTests: true,
      shouldFailFast: false,
      shouldPrintSkipped: false,
      shouldRandomizeTestOrder: false,
      shouldSilenceUnsupported: false,
      warnSlowerThan: 0,
    );
  }

  factory TestFlags.device(List<String> tNames) {
    return TestFlags(
      testNames: tNames,
      dryRun: false,
      limit: 0,
      isVerbose: false,
      allOutput: false,
      simpleOutput: true,
      shouldOnlyRunDeviceTests: true,
      shouldOnlyRunHostTests: false,
      shouldFailFast: false,
      shouldPrintSkipped: false,
      shouldRandomizeTestOrder: false,
      shouldSilenceUnsupported: false,
      warnSlowerThan: 0,
    );
  }

  Iterable<PermutatedTestFlags> get permutations sync* {
    // Check for having zero `testName` instances, which indicates that the
    // developer wants a wide-open run that includes as many tests as possible
    if (testNames.isEmpty) {
      yield PermutatedTestFlags(
        dryRun: dryRun,
        limit: 0,
        isVerbose: isVerbose,
        allOutput: allOutput,
        simpleOutput: simpleOutput,
        shouldFailFast: shouldFailFast,
        shouldOnlyRunDeviceTests: shouldOnlyRunDeviceTests,
        shouldOnlyRunHostTests: shouldOnlyRunHostTests,
        shouldPrintSkipped: shouldPrintSkipped,
        shouldRandomizeTestOrder: shouldRandomizeTestOrder,
        shouldSilenceUnsupported: shouldSilenceUnsupported,
        testName: null,
        warnSlowerThan: warnSlowerThan,
      );
      return;
    }
    for (String testName in testNames) {
      yield PermutatedTestFlags(
        dryRun: dryRun,
        isVerbose: isVerbose,
        limit: 0,
        allOutput: allOutput,
        simpleOutput: simpleOutput,
        shouldFailFast: shouldFailFast,
        shouldOnlyRunDeviceTests: shouldOnlyRunDeviceTests,
        shouldOnlyRunHostTests: shouldOnlyRunHostTests,
        shouldPrintSkipped: shouldPrintSkipped,
        shouldRandomizeTestOrder: shouldRandomizeTestOrder,
        shouldSilenceUnsupported: shouldSilenceUnsupported,
        testName: testName,
        warnSlowerThan: warnSlowerThan,
      );
    }
  }
}

/// An expanded set of flags passed to `fx test` against which all available
/// tests will be examined.
class PermutatedTestFlags extends _FlagsMixin {
  String testName;
  PermutatedTestFlags({
    @required this.testName,
    dryRun,
    isVerbose,
    limit,
    allOutput,
    simpleOutput,
    shouldFailFast,
    shouldOnlyRunDeviceTests,
    shouldOnlyRunHostTests,
    shouldPrintSkipped,
    shouldRandomizeTestOrder,
    shouldSilenceUnsupported,
    warnSlowerThan,
  }) : super(
          dryRun: dryRun,
          isVerbose: isVerbose,
          limit: limit,
          allOutput: allOutput,
          simpleOutput: simpleOutput,
          shouldFailFast: shouldFailFast,
          shouldOnlyRunDeviceTests: shouldOnlyRunDeviceTests,
          shouldOnlyRunHostTests: shouldOnlyRunHostTests,
          shouldPrintSkipped: shouldPrintSkipped,
          shouldRandomizeTestOrder: shouldRandomizeTestOrder,
          shouldSilenceUnsupported: shouldSilenceUnsupported,
          warnSlowerThan: warnSlowerThan,
        );

  @override
  String toString() {
    var chunks = <String>[
      if (testName != null) testName,
      if (shouldOnlyRunDeviceTests) '-d',
      if (shouldOnlyRunHostTests) '-h',
    ];
    var chunksStr = chunks.isNotEmpty ? ' ${chunks.join(" ")}' : '';
    return '<PermuatedTestFlags$chunksStr>';
  }
}
