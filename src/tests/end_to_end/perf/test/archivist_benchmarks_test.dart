// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:test/test.dart';

import 'helpers.dart';

void main() {
  enableLoggingOutput();

  test('archivist_redaction_benchmarks', () async {
    final helper = await PerfTestHelper.make();
    await helper.runTestComponent(
        packageName: 'archivist-redaction-benchmarks',
        componentName: 'archivist-redaction-benchmarks.cmx',
        commandArgs: PerfTestHelper.componentOutputPath);
  }, timeout: Timeout.none);

  test('archivist_logging_benchmarks', () async {
    final helper = await PerfTestHelper.make();
    await helper.runTestComponent(
        packageName: 'archivist-logging-benchmarks',
        componentName: 'archivist-logging-benchmarks.cmx',
        commandArgs: PerfTestHelper.componentOutputPath);
  }, timeout: Timeout.none);
}
