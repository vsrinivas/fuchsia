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

    memory.model.updateMem(_buildStats(0.5));
    Spec spec = await memory.getSpec();
    expect(spec.groups.first.title, isNotNull);
    expect(spec.groups.first.values.isEmpty, false);

    ProgressValue progress = spec.groups.first.values
        .where((v) => v.$tag == ValueTag.progress)
        .first
        ?.progress;
    expect(progress, isNotNull);
    expect(progress?.value, greaterThan(0));
    expect(progress?.value, lessThan(1));
    expect(progress.value, 0.5);

    // Update memory usage.
    memory.model.updateMem(_buildStats(0.7));

    spec = await memory.getSpec();
    expect(spec.groups.first.title, isNotNull);
    expect(spec.groups.first.values.isEmpty, false);

    progress = spec.groups.first.values
        .where((v) => v.$tag == ValueTag.progress)
        .first
        ?.progress;
    expect(progress, isNotNull);
    expect(progress?.value, greaterThan(0));
    expect(progress?.value, lessThan(1));
    expect(progress.value, moreOrLessEquals(0.3, epsilon: 1e-5));

    memory.dispose();
  });
}

int get gB => pow(1024, 3);

mem.Stats _buildStats(double used) {
  // ignore: missing_required_param, missing_required_param_with_details
  return mem.Stats(
    totalBytes: 1 * gB,
    freeBytes: (used * gB).toInt(),
  );
}

// Mock classes.
class MockMonitorProxy extends Mock implements mem.MonitorProxy {}

class MockBinding extends Mock implements mem.WatcherBinding {}
