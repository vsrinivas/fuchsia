// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:test/test.dart';

import 'helpers.dart';

void main() {
  enableLoggingOutput();

  test('fidl_roundtrip', () async {
    final helper = await PerfTestHelper.make();
    const resultsFile = '/tmp/fidl_roundtrip.json';
    final result = await helper.sl4fDriver.ssh
        .run('/bin/roundtrip_fidl_benchmarks $resultsFile');
    expect(result.exitCode, equals(0));
    await helper.processResults(resultsFile);
  }, timeout: Timeout.none);
}
