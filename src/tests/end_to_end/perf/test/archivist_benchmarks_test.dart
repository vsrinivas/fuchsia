// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TODO(https://fxbug.dev/84961): Fix null safety and remove this language version.
// @dart=2.9

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
