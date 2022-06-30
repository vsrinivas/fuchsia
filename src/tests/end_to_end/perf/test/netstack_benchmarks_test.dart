// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TODO(https://fxbug.dev/84961): Fix null safety and remove this language version.
// @dart=2.9

import 'package:test/test.dart';

import 'helpers.dart';

void main() {
  enableLoggingOutput();

  test('socket_benchmarks', () async {
    final helper = await PerfTestHelper.make();
    await helper.runTestComponent(
        packageName: 'socket-benchmarks',
        componentName: 'socket-benchmarks.cm',
        commandArgs: '-p --quiet --out ${PerfTestHelper.componentOutputPath}');
  }, timeout: Timeout.none);

  test('socket_benchmarks_with_fast_udp', () async {
    final helper = await PerfTestHelper.make();
    await helper.runTestComponent(
        packageName: 'socket-benchmarks-with-fast-udp',
        componentName: 'socket-benchmarks-with-fast-udp.cm',
        commandArgs: '-p --quiet --out ${PerfTestHelper.componentOutputPath}');
  }, timeout: Timeout.none);

  test('socket_benchmarks_with_fake_netstack', () async {
    final helper = await PerfTestHelper.make();
    await helper.runTestComponent(
        packageName: 'socket-benchmarks-with-fake-netstack',
        componentName: 'socket-benchmarks-with-fake-netstack.cm',
        commandArgs: '-p --quiet --out ${PerfTestHelper.componentOutputPath}');
  }, timeout: Timeout.none);

  test('udp_serde_benchmarks', () async {
    final helper = await PerfTestHelper.make();
    await helper.runTestComponent(
        packageName: 'udp-serde-benchmarks',
        componentName: 'udp-serde-benchmarks.cm',
        commandArgs: '-p --quiet --out ${PerfTestHelper.componentOutputPath}');
  }, timeout: Timeout.none);
}
