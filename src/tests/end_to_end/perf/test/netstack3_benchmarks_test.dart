// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TODO(https://fxbug.dev/84961): Fix null safety and remove this language version.
// @dart=2.9

import 'package:test/test.dart';

import 'helpers.dart';

void main() {
  enableLoggingOutput();

  test('netstack3_benchmarks', () async {
    final helper = await PerfTestHelper.make();
    await helper.runTestComponent(
        packageName: 'netstack3_benchmarks',
        componentName: 'netstack3_benchmarks.cm',
        commandArgs: PerfTestHelper.componentOutputPath,
        expectedMetricNamesFile: 'fuchsia.netstack3.core.txt');
  }, timeout: Timeout.none);
}
