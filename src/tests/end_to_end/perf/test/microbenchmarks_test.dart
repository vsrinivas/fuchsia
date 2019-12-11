// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:test/test.dart';

import 'helpers.dart';

void main() {
  enableLoggingOutput();

  test('zircon_benchmarks', () async {
    final helper = await PerfTestHelper.make();
    const resultsFile = '/tmp/perf_results.json';
    // Log the full 32-bit exit status value in order to debug the flaky failure
    // in fxb/39577.  Do that on the Fuchsia side because the exit status we get
    // back from SSH is apparently truncated to 8 bits.
    const logStatus = r'status=$?; '
        r'echo exit status: $status; '
        r'if [ $status != 0 ]; then exit 1; fi';
    final result = await helper.sl4fDriver.ssh.run(
        '/bin/zircon_benchmarks -p --quiet --out $resultsFile; $logStatus');
    expect(result.exitCode, equals(0));
    await helper.processResults(resultsFile);
  }, timeout: Timeout.none);
}
