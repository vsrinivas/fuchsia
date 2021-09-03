// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:test/test.dart';

import 'helpers.dart';

void main() {
  enableLoggingOutput();

  test('fuchsia.kernel.boot', () async {
    final helper = await PerfTestHelper.make();
    await helper.runTestComponent(
        packageName: 'kernel-boot-benchmarks',
        componentName: 'kernel-boot-benchmarks.cmx',
        commandArgs: PerfTestHelper.componentOutputPath);
  }, timeout: Timeout.none);
}
