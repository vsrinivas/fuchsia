// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:test/test.dart';

import 'helpers.dart';

void main() {
  enableLoggingOutput();

  test('network_device_microbenchmarks', () async {
    final helper = await PerfTestHelper.make();

    const realmName = 'perftest';
    const packageName = 'network-device-microbenchmarks';
    const componentName = 'network-device-microbenchmarks.cmx';
    const resultsFile = 'perftest_results.json';
    const targetOutputPath =
        '/tmp/r/sys/r/$realmName/fuchsia.com:$packageName:0#meta:$componentName/$resultsFile';
    const command = 'run-test-component --realm-label=$realmName '
        'fuchsia-pkg://fuchsia.com/$packageName#meta/$componentName'
        ' -- -p --quiet --out /tmp/$resultsFile';

    final result = await helper.sl4fDriver.ssh.run(command);
    expect(result.exitCode, equals(0));
    await helper.processResults(targetOutputPath);
  }, timeout: Timeout.none);
}
