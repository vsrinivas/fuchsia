// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:io';

import 'package:quiver/testing/async.dart';
import 'package:test/test.dart';
import 'package:mockito/mockito.dart';

import 'package:sl4f/sl4f.dart';

class MockDump extends Mock implements Dump {}

class MockIOSink extends Mock implements IOSink {}

class MockSsh extends Mock implements Ssh {}

void main() {
  // CF-876
  test('diagnostics terminate do not hang even if the tools do', () {
    const eventually = Duration(hours: 1);

    FakeAsync().run((async) {
      final dump = MockDump();
      final ssh = MockSsh();
      final sl4f = Sl4f('', ssh);

      when(dump.openForWrite(any, any)).thenAnswer((_) => MockIOSink());
      when(ssh.start(any)).thenAnswer((_) =>
          // Use sleep 60 (1 minute) to simulate a hanging process. The sleep
          // occurs in system time whereas our test executes in fake time
          // (instantaneous), so this sleep is effectively infinite.
          Process.start('/bin/sleep', ['60']));

      expect(sl4f.dumpDiagnostics('dumpName', dump: dump).timeout(eventually),
          completes);
      async.elapse(eventually);
    });
  });
}
