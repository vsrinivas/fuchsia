// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TODO(https://fxbug.dev/84961): Fix null safety and remove this language version.
// @dart=2.9

import 'package:fxtest/fxtest.dart';
import 'package:meta/meta.dart';

/// Utility which applies sanity checks to make sure we both 1) actually want
/// to execute tests, and 2) are set up to get correct output.
// ignore: one_member_abstracts
abstract class Checklist {
  Future<bool> isDeviceReady(List<TestBundle> bundles);
}

class PreChecker implements Checklist {
  final Function(TestEvent) eventSink;
  final TestsConfig testsConfig;

  PreChecker({
    @required this.eventSink,
    @required this.testsConfig,
  });

  factory PreChecker.fromConfig(
    TestsConfig testsConfig, {
    @required Function(TestEvent) eventSink,
  }) {
    return PreChecker(
      testsConfig: testsConfig,
      eventSink: eventSink,
    );
  }

  bool hasDeviceTests(List<TestBundle> testBundles) {
    return testBundles
        .any((e) => !hostTestTypes.contains(e.testDefinition.testType));
  }

  @override
  Future<bool> isDeviceReady(List<TestBundle> testBundles) async {
    if (!hasDeviceTests(testBundles)) return true;

    // check for a running update server
    bool isPackageServerRunning = await fxCommandRunWithIO(
      eventSink,
      testsConfig.wrapWith,
      testsConfig.fxEnv.fx,
      'is-package-server-running',
    ).then((process) => process.exitCode).then((exitCode) => exitCode == 0);
    if (!isPackageServerRunning) {
      return false;
    }
    if (testsConfig.flags.shouldUpdateIfInBase) {
      // if any test is on base, perform an OTA first
      Iterable<String> allTestNames = testBundles
          .where((e) => e.testDefinition.testType != TestType.host)
          .map((e) => e.testDefinition.name);
      // TODO: update-if-in-base can't handle large numbers of command-line
      // arguments. The code below paginates and calls it with batches of
      // 50 non-host tests to avoid command line buffer issues. In the future,
      // update-if-in-base and is-package-server-running should have pure Dart
      // implementations.
      Iterable<String> batch;
      while ((batch = allTestNames.take(50)).isNotEmpty) {
        bool result = await fxCommandRunWithIO(
          eventSink,
          testsConfig.wrapWith,
          testsConfig.fxEnv.fx,
          'update-if-in-base',
          batch.toList(),
        ).then((process) => process.exitCode).then((exitCode) => exitCode == 0);
        if (!result) {
          return false;
        }
        allTestNames = allTestNames.skip(50);
      }
    }
    return true;
  }
}
