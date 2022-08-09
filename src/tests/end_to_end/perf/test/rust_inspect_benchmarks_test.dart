// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TODO(https://fxbug.dev/84961): Fix null safety and remove this language version.
// @dart=2.9

import 'package:test/test.dart';

import 'helpers.dart';

void main() {
  enableLoggingOutput();

  test('rust_inspect_reader_benchmarks', () async {
    final helper = await PerfTestHelper.make();
    await helper.runTestComponent(
        packageName: 'rust-inspect-benchmarks',
        componentName: 'reader.cm',
        commandArgs: PerfTestHelper.componentOutputPath,
        expectedMetricNamesFile: 'fuchsia.rust_inspect.reader_benchmarks.txt');
  }, timeout: Timeout.none);

  test('rust_inspect_writer_benchmarks', () async {
    final helper = await PerfTestHelper.make();
    await helper.runTestComponent(
        packageName: 'rust-inspect-benchmarks',
        componentName: 'writer.cm',
        commandArgs: PerfTestHelper.componentOutputPath,
        expectedMetricNamesFile: 'fuchsia.rust_inspect.benchmarks.txt');
  }, timeout: Timeout.none);

  test('rust_inspect_snapshot_filter_benchmarks', () async {
    final helper = await PerfTestHelper.make();
    await helper.runTestComponent(
        packageName: 'rust-inspect-benchmarks',
        componentName: 'snapshot_filter.cm',
        commandArgs: PerfTestHelper.componentOutputPath,
        expectedMetricNamesFile: 'fuchsia.rust_inspect.selectors.txt');
  }, timeout: Timeout.none);
}
