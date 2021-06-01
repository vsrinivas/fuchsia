// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:fidl/fidl.dart';
import 'package:fidl_fuchsia_memory/fidl_async.dart' as mem;
import 'package:flutter_test/flutter_test.dart';
import 'package:mockito/mockito.dart';
import 'package:settings/settings.dart';

void main() {
  test('System Information', () async {
    final monitorProxy = MockMonitorProxy();
    final binding = MockBinding();
    final sysInfo = SystemInformation(monitor: monitorProxy, binding: binding);
    var spec = await sysInfo.getSpec();

    // Should receive system information spec.
    expect(spec.title, isNotNull);
    expect(spec.groups?.first.values?.first.text?.text, isNotNull);
    expect(spec.groups?.first.values?.first.text?.text, 'View');

    sysInfo.dispose();
  });
}

// Mock classes.
class MockMonitorProxy extends Mock implements mem.MonitorProxy {}

class MockBinding extends Mock implements mem.WatcherBinding {
  @override
  InterfaceHandle<mem.Watcher> wrap(mem.Watcher? impl) =>
      super.noSuchMethod(Invocation.method(#wrap, [impl]));
}
