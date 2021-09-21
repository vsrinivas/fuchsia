// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TODO(https://fxbug.dev/84961): Fix null safety and remove this language version.
// @dart=2.9

import 'package:test/test.dart';

import 'helpers.dart';

void main() {
  enableLoggingOutput();

  test('selectors_benchmarks', () async {
    final helper = await PerfTestHelper.make();
    await helper.runTestComponent(
        packageName: 'selectors-benchmarks',
        componentName: 'selectors-benchmarks.cmx',
        commandArgs: PerfTestHelper.componentOutputPath);
  }, timeout: Timeout.none);
}
