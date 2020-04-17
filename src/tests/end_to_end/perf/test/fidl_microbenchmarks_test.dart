// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:test/test.dart';

import 'helpers.dart';

void main() {
  enableLoggingOutput();

  test('go_fidl_microbenchmarks', () async {
    final helper = await PerfTestHelper.make();
    const resultsFile = '/tmp/perf_results.json';
    final result = await helper.sl4fDriver.ssh
        .run('/bin/go_fidl_benchmarks --out_file $resultsFile');
    expect(result.exitCode, equals(0));
    await helper.processResults(resultsFile);
  }, timeout: Timeout.none);

  test('hlcpp_fidl_microbenchmarks', () async {
    final helper = await PerfTestHelper.make();
    const resultsFile = '/tmp/perf_results.json';
    final result = await helper.sl4fDriver.ssh
        .run('/bin/hlcpp_fidl_benchmarks -p --out $resultsFile');
    expect(result.exitCode, equals(0));
    await helper.processResults(resultsFile);
  }, timeout: Timeout.none);

  test('rust_fidl_microbenchmarks', () async {
    final helper = await PerfTestHelper.make();
    const resultsFile = '/tmp/perf_results.json';
    final result = await helper.sl4fDriver.ssh
        .run('/bin/rust_fidl_benchmarks $resultsFile');
    expect(result.exitCode, equals(0));
    await helper.processResults(resultsFile);
  }, timeout: Timeout.none);
}
