// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:math';

import 'package:fidl_fuchsia_ui_remotewidgets/fidl_async.dart';
import 'package:fidl_fuchsia_memory/fidl_async.dart' as mem;
import 'package:flutter_test/flutter_test.dart';
import 'package:settings/settings.dart';
import 'package:mockito/mockito.dart';

void main() {
  test('Memory', () async {
    mem.MonitorProxy monitorProxy = MockMonitorProxy();
    mem.WatcherBinding binding = MockBinding();
    Memory memory = Memory(monitor: monitorProxy, binding: binding);

    final mem.Watcher watcher =
        verify(binding.wrap(captureAny)).captured.single;
    await watcher.onChange(_buildStats(0.5));

    // Should receive memory spec
    Spec spec = await memory.getSpec();
    expect(spec.groups.first.title, isNotNull);
    expect(spec.groups.first.values.isEmpty, false);

    // Confirm progress value is correct
    ProgressValue progress = spec.groups.first.values
        .where((v) => v.$tag == ValueTag.progress)
        .first
        ?.progress;
    expect(progress, isNotNull);
    expect(progress?.value, greaterThan(0));
    expect(progress?.value, lessThan(1));
    expect(progress.value, 0.5);

    // Confirm text value is correct
    TextValue text = spec.groups.first.values
        .where((v) => v.$tag == ValueTag.text)
        .first
        ?.text;
    expect(text?.text, '0.500GB / 1.00GB');

    memory.dispose();
  });

  test('Change Memory', () async {
    mem.MonitorProxy monitorProxy = MockMonitorProxy();
    mem.WatcherBinding binding = MockBinding();
    Memory memory = Memory(monitor: monitorProxy, binding: binding);

    final mem.Watcher watcher =
        verify(binding.wrap(captureAny)).captured.single;
    await watcher.onChange(_buildStats(0.5));

    // Should receive memory spec
    Spec spec = await memory.getSpec();
    expect(spec.groups.first.title, isNotNull);
    expect(spec.groups.first.values.isEmpty, false);

    // Confirm progress value is correct
    ProgressValue progress = spec.groups.first.values
        .where((v) => v.$tag == ValueTag.progress)
        .first
        ?.progress;
    expect(progress, isNotNull);
    expect(progress?.value, greaterThan(0));
    expect(progress?.value, lessThan(1));
    expect(progress.value, 0.5);

    // Confirm text value is correct
    TextValue text = spec.groups.first.values
        .where((v) => v.$tag == ValueTag.text)
        .first
        ?.text;
    expect(text?.text, '0.500GB / 1.00GB');

    // Update memory usage.
    await watcher.onChange(_buildStats(0.7));

    spec = await memory.getSpec();
    expect(spec.groups.first.title, isNotNull);
    expect(spec.groups.first.values.isEmpty, false);

    // Confirm progress value is correct
    progress = spec.groups.first.values
        .where((v) => v.$tag == ValueTag.progress)
        .first
        ?.progress;
    expect(progress, isNotNull);
    expect(progress?.value, greaterThan(0));
    expect(progress?.value, lessThan(1));
    expect(progress.value, moreOrLessEquals(0.3, epsilon: 1e-5));

    // Confirm text value is correct
    text = spec.groups.first.values
        .where((v) => v.$tag == ValueTag.text)
        .first
        ?.text;
    expect(text?.text, '0.300GB / 1.00GB');

    memory.dispose();
  });

  test('Min Memory', () async {
    mem.MonitorProxy monitorProxy = MockMonitorProxy();
    mem.WatcherBinding binding = MockBinding();
    Memory memory = Memory(monitor: monitorProxy, binding: binding);

    final mem.Watcher watcher =
        verify(binding.wrap(captureAny)).captured.single;
    await watcher.onChange(_buildStats(1));

    // Should receive memory spec
    Spec spec = await memory.getSpec();
    expect(spec.groups.first.title, isNotNull);
    expect(spec.groups.first.values.isEmpty, false);

    // Confirm progress value is correct
    ProgressValue progress = spec.groups.first.values
        .where((v) => v.$tag == ValueTag.progress)
        .first
        ?.progress;
    expect(progress, isNotNull);
    expect(progress?.value, lessThan(1));
    expect(progress.value, 0);

    // Confirm text value is correct
    TextValue text = spec.groups.first.values
        .where((v) => v.$tag == ValueTag.text)
        .first
        ?.text;
    expect(text?.text, '0.00GB / 1.00GB');

    memory.dispose();
  });

  test('Max Memory', () async {
    mem.MonitorProxy monitorProxy = MockMonitorProxy();
    mem.WatcherBinding binding = MockBinding();
    Memory memory = Memory(monitor: monitorProxy, binding: binding);

    final mem.Watcher watcher =
        verify(binding.wrap(captureAny)).captured.single;
    await watcher.onChange(_buildStats(0));

    // Should receive memory spec
    Spec spec = await memory.getSpec();
    expect(spec.groups.first.title, isNotNull);
    expect(spec.groups.first.values.isEmpty, false);

    // Confirm progress value is correct
    ProgressValue progress = spec.groups.first.values
        .where((v) => v.$tag == ValueTag.progress)
        .first
        ?.progress;
    expect(progress, isNotNull);
    expect(progress?.value, greaterThan(0));
    expect(progress.value, 1);

    // Confirm text value is correct
    TextValue text = spec.groups.first.values
        .where((v) => v.$tag == ValueTag.text)
        .first
        ?.text;
    expect(text?.text, '1.00GB / 1.00GB');

    memory.dispose();
  });
}

int get gB => pow(1024, 3);

mem.Stats _buildStats(double bytes) {
  // ignore: missing_required_param, missing_required_param_with_details
  return mem.Stats(
    totalBytes: 1 * gB,
    freeBytes: (bytes * gB).toInt(),
  );
}

// Mock classes.
class MockMonitorProxy extends Mock implements mem.MonitorProxy {}

class MockBinding extends Mock implements mem.WatcherBinding {}
