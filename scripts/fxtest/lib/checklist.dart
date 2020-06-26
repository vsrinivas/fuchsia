// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:fxtest/fxtest.dart';
import 'package:io/ansi.dart';
import 'package:meta/meta.dart';

/// Utility which verifies that we are prepared to successfully run our desired
/// tests.
abstract class Checklist {
  /// Checks for a running package server and returns `true` if one is found.
  Future<bool> isPackageServerRunning();

  /// Attempts to update packages and returns `true` if that completes
  /// successfully.
  Future<bool> maybeUpdateBasePackages(List<TestBundle> bundles);
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

  @override
  Future<bool> isPackageServerRunning() =>
      testsConfig.fx.isPackageServerRunning();

  @override
  Future<bool> maybeUpdateBasePackages(List<TestBundle> testBundles) async {
    if (!testsConfig.flags.shouldUpdateIfInBase) {
      return true;
    }
    Iterable<String> allDeviceTestNames = testBundles
        .where((e) => e.testDefinition.testType != TestType.host)
        .map((e) => e.testDefinition.name);
    if (testsConfig.flags.isVerbose) {
      eventSink(TestInfo(testsConfig.wrapWith(
        '> fx update-if-in-base ${allDeviceTestNames.toList().join(' ')}\n',
        [green, styleBold],
      )));
    } else {
      final packages = allDeviceTestNames.length != 1 ? 'packages' : 'package';
      eventSink(TestInfo(
          '> running \'fx update-if-in-base\' with  ${allDeviceTestNames.length}'
          ' $packages. (Use `-v` to see which packages)\n'));
    }
    return testsConfig.fx.updateIfInBase(allDeviceTestNames.toList());
  }
}
