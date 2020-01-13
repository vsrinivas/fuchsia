// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:test/test.dart';

import 'helpers.dart';

void main() {
  enableLoggingOutput();

  void addTest(String label, String componentUrl) {
    test(label, () async {
      final helper = await PerfTestHelper.make();
      const resultsFile = '/tmp/perf_results.json';
      final result = await helper.sl4fDriver.ssh
          .run('/bin/run $componentUrl --out_file $resultsFile'
              ' --benchmark_label $label');
      expect(result.exitCode, equals(0));
      await helper.processResults(resultsFile);
    }, timeout: Timeout.none);
  }

  const baseUrl = 'fuchsia-pkg://fuchsia.com/garnet_input_latency_benchmarks';

  addTest('fuchsia.input_latency.simplest_app',
      '$baseUrl#meta/run_simplest_app_benchmark.cmx');
  addTest('fuchsia.input_latency.yuv_to_image_pipe',
      '$baseUrl#meta/run_yuv_to_image_pipe_benchmark.cmx');
}
