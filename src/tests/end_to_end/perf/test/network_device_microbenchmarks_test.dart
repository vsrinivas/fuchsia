// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:test/test.dart';

import 'helpers.dart';

void main() {
  enableLoggingOutput();

  test('network_device_microbenchmarks', () async {
    final helper = await PerfTestHelper.make();
    await helper.runTestComponent(
        packageName: 'network-device-microbenchmarks',
        componentName: 'network-device-microbenchmarks.cmx',
        commandArgs: '-p --quiet --out ${PerfTestHelper.componentOutputPath}');
  }, timeout: Timeout.none);
}
