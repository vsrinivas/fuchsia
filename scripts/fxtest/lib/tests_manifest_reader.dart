// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:convert';
import 'dart:io';

import 'package:io/ansi.dart';
import 'package:fxtest/fxtest.dart';
import 'package:meta/meta.dart';
import 'package:path/path.dart' as p;

/// Harness for the completely processed tests manifest from a Fuchsia build.
class ParsedManifest {
  /// The raw JSON of a test plopped into a class for structured analysis.
  final List<TestDefinition> testDefinitions;

  /// The runnable wrappers that encapsulate a Fuchsia test.
  final List<TestRunner> testRunners;

  /// Number of test entries in the manifest that would indicate duplicate work.
  final int numDuplicateTests;

  /// Number of test entries that contained data structured outside the bounds
  /// of this script's capabilities. This number should be 0.
  final int numUnparsedTests;

  ParsedManifest({
    @required this.testDefinitions,
    @required this.testRunners,
    this.numDuplicateTests,
    this.numUnparsedTests,
  });
}

class TestsManifestReader {
  List<Checker> checkers;

  TestsManifestReader() {
    checkers = [
      NoArgumentsChecker(),
      ComponentTestChecker(),
      FullUrlComponentChecker(),
      UrlNameComponentChecker(),
      NameMatchChecker(),
      PathMatchChecker(),
    ];
  }

  /// Reads and parses the tests manifest file at `manifestLocation`.
  Future<List<TestDefinition>> loadTestsJson({
    String buildDir,
    String manifestFileName,
  }) async {
    List<dynamic> testJson = await readManifest(
      p.join(buildDir, manifestFileName),
    );
    return parseManifest(testJson, buildDir);
  }

  /// Finishes loading the raw test manifest into a list of usable objects.
  List<TestDefinition> parseManifest(List<dynamic> testJson, String buildDir) {
    return [
      for (var data in testJson)
        TestDefinition.fromJson(
          buildDir: buildDir,
          data: Map<String, dynamic>.from(data),
        )
    ];
  }

  /// Reads the manifest file off disk and parses its content as JSON
  Future<List<dynamic>> readManifest(
    String manifestLocation,
  ) async {
    return jsonDecode(await File(manifestLocation).readAsString());
  }

  /// Loops over the provided list of [TestDefinition]s and, based on the
  /// results of all registered [Checker]s, returns a list of [TestRunner]s.
  ParsedManifest aggregateTests({
    @required List<TestDefinition> testDefinitions,
    @required void Function(TestEvent) eventEmitter,
    @required TestFlags testFlags,
    @required String buildDir,
  }) {
    List<TestRunner> testRunners = [];
    Set<String> seenPackages = {};
    int numDuplicateTests = 0;
    int numUnparsedTests = 0;

    // This triple-loop may seem scary, but we:
    //  1. Always short-circuit once a test has been claimed, and
    //  2. Are dealing low upper-bounds loops
    //      - TestDefinitions (the outer loop) could be long for a
    //        large build, but
    //      - PermutatedFlags (the middle loop) will often be short
    //        (1 to 3 entries), and
    //      - Checkers (the innermost loop) is defined in code and unlikely to
    //        ever exceed a half-dozen
    bool testIsClaimed;
    for (var testDefinition in testDefinitions) {
      // This implies that we encountered a test definition with no code
      // to support its parsing and execution. It definitely implies a critical
      // failure that we should immediately correct.
      if (testDefinition.executionHandle.isUnsupported) {
        numUnparsedTests += 1;
        String redError = '${wrapWith("Error:", [red])} '
            'Could not parse test:\n$testDefinition';
        if (testFlags.shouldSilenceUnsupported) {
          if (testFlags.isVerbose) {
            eventEmitter(TestInfo(redError));
          }
          continue;
        } else {
          String fxTest = wrapWith('fx test', [blue, styleBold]);
          String dashU = wrapWith('-u', [blue, styleBold]);
          redError +=
              '\n\nThis is very likely a problem with the $fxTest script'
              ' or the test itself, and is not of any error on your part.'
              '\nPlease submit a bug to report this unparsed test to the'
              ' Fuchsia team.\n\nPass the $dashU flag if you would like to'
              ' silence errors for unsupported tests and continue.';
          throw UnparsedTestException(redError);
        }
      }

      testIsClaimed = false;
      for (var permutatedTestFlags in testFlags.permutations) {
        // If a previous TestFlag configuration claimed this test, we definitely
        // don't care whether another would, as well. We don't want to run tests
        // more than once.
        if (testIsClaimed) break;

        for (var checker in checkers) {
          if (checker.canHandle(permutatedTestFlags, testDefinition)) {
            // Certain test definitions result in multiple entries in `tests.json`,
            // but invoking the test runner on their shared package name already
            // captures all tests. Therefore, any such sibling entry further down
            // `tests.json` will only result in duplicate work.
            if (seenPackages.contains(testDefinition.executionHandle.handle)) {
              numDuplicateTests += 1;
              testIsClaimed = true;
              break;
            } else {
              seenPackages.add(testDefinition.executionHandle.handle);
            }

            // Now that we know we're seeing this `packageName` for the first
            // time, we can add it to the queue
            testRunners.add(TestRunner(
              testDefinition,
              workingDirectory: buildDir,
              isDryRun: testFlags.dryRun,
              raiseOnFailure: testFlags.shouldFailFast,
            ));

            // Setting this flag breaks out of the Tier 2 (PermutatedTestFlags)
            // loop
            testIsClaimed = true;
            // Break out of the Tier 3 (Checkers) loop
            break;
          }
        }
      }

      if (!testIsClaimed && testFlags.shouldPrintSkipped) {
        eventEmitter(TestInfo('Skipped test:\n$testDefinition'));
      }
    }

    if (testFlags.shouldRandomizeTestOrder) {
      testRunners.shuffle();
    }

    return ParsedManifest(
      numDuplicateTests: numDuplicateTests,
      numUnparsedTests: numUnparsedTests,
      testDefinitions: testDefinitions,
      testRunners: testRunners,
    );
  }

  void reportOnTestRunners({
    @required ParsedManifest parsedManifest,
    @required TestFlags testFlags,
    @required void Function(TestEvent) eventEmitter,
    @required String userFriendlyBuildDir,
  }) {
    String duplicates = '';
    if (parsedManifest.numDuplicateTests > 0) {
      String duplicateWord =
          parsedManifest.numDuplicateTests == 1 ? 'duplicate' : 'duplicates';
      duplicates = wrapWith(
          ' (with ${parsedManifest.numDuplicateTests} $duplicateWord)',
          [darkGray]);
    }

    if (!testFlags.isVerbose && parsedManifest.numUnparsedTests > 0) {
      eventEmitter(TestInfo(
        'Found ${parsedManifest.numUnparsedTests.toString()} tests that '
        'could not be parsed.',
      ));
    }

    String manifestName = wrapWith('$userFriendlyBuildDir/tests.json', [green]);
    eventEmitter(TestInfo(
      'Found ${parsedManifest.testDefinitions.length} total '
      '${parsedManifest.testDefinitions.length != 1 ? "tests" : "test"} in '
      '$manifestName$duplicates',
    ));

    int numTests = testFlags.limit == 0
        ? parsedManifest.testRunners.length
        : testFlags.limit;
    eventEmitter(TestInfo(
      'Will run $numTests '
      '${parsedManifest.testRunners.length != 1 ? "tests" : "test"}',
    ));
  }
}
