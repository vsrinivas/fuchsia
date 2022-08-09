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
        packageName: 'socket-benchmarks-tests',
        componentName: 'socket-benchmarks.cm',
        commandArgs: '-p --quiet --out ${PerfTestHelper.componentOutputPath}',
        expectedMetricNamesFile: 'fuchsia.network.socket.loopback.txt');
  }, timeout: Timeout.none);

  test('socket_benchmarks_with_fast_udp', () async {
    final helper = await PerfTestHelper.make();
    await helper.runTestComponent(
        packageName: 'socket-benchmarks-tests',
        componentName: 'socket-benchmarks-with-fast-udp.cm',
        commandArgs: '-p --quiet --out ${PerfTestHelper.componentOutputPath}',
        expectedMetricNamesFile: 'fuchsia.network.socket.loopback.fastudp.txt');
  }, timeout: Timeout.none);

  test('socket_benchmarks_with_netstack3', () async {
    final helper = await PerfTestHelper.make();
    await helper.runTestComponent(
        packageName: 'socket-benchmarks-tests',
        componentName: 'socket-benchmarks-with-netstack3.cm',
        commandArgs: '-p --quiet --out ${PerfTestHelper.componentOutputPath}',
        expectedMetricNamesFile:
            'fuchsia.network.socket.loopback.netstack3.txt');
  }, timeout: Timeout.none);

  test('socket_benchmarks_with_fake_netstack', () async {
    final helper = await PerfTestHelper.make();
    await helper.runTestComponent(
        packageName: 'socket-benchmarks-tests',
        componentName: 'socket-benchmarks-with-fake-netstack.cm',
        commandArgs: '-p --quiet --out ${PerfTestHelper.componentOutputPath}',
        expectedMetricNamesFile:
            'fuchsia.network.socket.loopback.fake_netstack.txt');
  }, timeout: Timeout.none);

  test('udp_serde_benchmarks', () async {
    final helper = await PerfTestHelper.make();
    await helper.runTestComponent(
        packageName: 'udp-serde-benchmarks',
        componentName: 'udp-serde-benchmarks.cm',
        commandArgs: '-p --quiet --out ${PerfTestHelper.componentOutputPath}',
        expectedMetricNamesFile: 'fuchsia.network.udp_serde.txt');
  }, timeout: Timeout.none);
}
