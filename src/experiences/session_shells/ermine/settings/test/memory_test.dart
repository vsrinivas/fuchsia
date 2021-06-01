// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:math';

import 'package:fidl/fidl.dart';
import 'package:fidl_fuchsia_memory/fidl_async.dart' as mem;
import 'package:fidl_fuchsia_ui_remotewidgets/fidl_async.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:mockito/mockito.dart';
import 'package:settings/settings.dart';

void main() {
  test('Memory', () async {
    final monitorProxy = MockMonitorProxy();
    final binding = MockBinding();
    Memory memory = Memory(monitor: monitorProxy, binding: binding);

    final mem.Watcher watcher =
        verify(binding.wrap(captureAny)).captured.single;
    await watcher.onChange(_buildStats(0.5));

    // Should receive memory spec
    Spec spec = await memory.getSpec();
    expect(spec.groups?.first.title, isNotNull);
    expect(spec.groups?.first.values?.isEmpty, false);

    // Confirm text value is correct
    TextValue? text = spec.groups?.first.values
        ?.where((v) => v.$tag == ValueTag.text)
        .first
        .text;
    expect(text?.text, '0.500GB / 1.00GB');

    memory.dispose();
  });

  test('Change Memory', () async {
    final monitorProxy = MockMonitorProxy();
    final binding = MockBinding();
    Memory memory = Memory(monitor: monitorProxy, binding: binding);

    final mem.Watcher watcher =
        verify(binding.wrap(captureAny)).captured.single;
    await watcher.onChange(_buildStats(0.5));

    // Should receive memory spec
    Spec spec = await memory.getSpec();
    expect(spec.groups?.first.title, isNotNull);
    expect(spec.groups?.first.values?.isEmpty, false);

    // Confirm text value is correct
    TextValue? text = spec.groups?.first.values
        ?.where((v) => v.$tag == ValueTag.text)
        .first
        .text;
    expect(text?.text, '0.500GB / 1.00GB');

    // Update memory usage.
    await watcher.onChange(_buildStats(0.7));

    spec = await memory.getSpec();
    expect(spec.groups?.first.title, isNotNull);
    expect(spec.groups?.first.values?.isEmpty, false);

    // Confirm text value is correct
    text = spec.groups?.first.values
        ?.where((v) => v.$tag == ValueTag.text)
        .first
        .text;
    expect(text?.text, '0.300GB / 1.00GB');

    memory.dispose();
  });

  test('Min Memory', () async {
    final monitorProxy = MockMonitorProxy();
    final binding = MockBinding();
    Memory memory = Memory(monitor: monitorProxy, binding: binding);

    final mem.Watcher watcher =
        verify(binding.wrap(captureAny)).captured.single;
    await watcher.onChange(_buildStats(1));

    // Should receive memory spec
    Spec spec = await memory.getSpec();
    expect(spec.groups?.first.title, isNotNull);
    expect(spec.groups?.first.values?.isEmpty, false);

    // Confirm text value is correct
    TextValue? text = spec.groups?.first.values
        ?.where((v) => v.$tag == ValueTag.text)
        .first
        .text;
    expect(text?.text, '0.00GB / 1.00GB');

    memory.dispose();
  });

  test('Max Memory', () async {
    final monitorProxy = MockMonitorProxy();
    final binding = MockBinding();
    Memory memory = Memory(monitor: monitorProxy, binding: binding);

    final mem.Watcher watcher =
        verify(binding.wrap(captureAny)).captured.single;
    await watcher.onChange(_buildStats(0));

    // Should receive memory spec
    Spec spec = await memory.getSpec();
    expect(spec.groups?.first.title, isNotNull);
    expect(spec.groups?.first.values?.isEmpty, false);

    // Confirm text value is correct
    TextValue? text = spec.groups?.first.values
        ?.where((v) => v.$tag == ValueTag.text)
        .first
        .text;
    expect(text?.text, '1.00GB / 1.00GB');

    memory.dispose();
  });
}

int get gB => pow(1024, 3).toInt();

mem.Stats _buildStats(double bytes) {
  return mem.Stats(
    totalBytes: 1 * gB,
    freeBytes: (bytes * gB).toInt(),
    wiredBytes: 0,
    totalHeapBytes: 0,
    freeHeapBytes: 0,
    vmoBytes: 0,
    mmuOverheadBytes: 0,
    ipcBytes: 0,
    otherBytes: 0,
  );
}

// Mock classes.
class MockMonitorProxy extends Mock implements mem.MonitorProxy {}

class MockBinding extends Mock implements mem.WatcherBinding {
  @override
  InterfaceHandle<mem.Watcher> wrap(mem.Watcher? impl) =>
      super.noSuchMethod(Invocation.method(#wrap, [impl]));
}
