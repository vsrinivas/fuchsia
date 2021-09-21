// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TODO(https://fxbug.dev/84961): Fix null safety and remove this language version.
// @dart=2.9

import 'package:sl4f/sl4f.dart' as sl4f;
import 'package:test/test.dart';

void main() {
  sl4f.Sl4f sl4fDriver;
  sl4f.FeedbackDataProvider feedback;

  setUp(() async {
    sl4fDriver = sl4f.Sl4f.fromEnvironment();
    await sl4fDriver.startServer();
    feedback = sl4f.FeedbackDataProvider(sl4fDriver);
  });

  tearDown(() async {
    await sl4fDriver.stopServer();
    sl4fDriver.close();
  });

  test('snapshot contains inspect data', () async {
    final snapshot = await feedback.getSnapshot();
    final result = snapshot.inspect
        .where((entry) =>
            entry.containsKey('moniker') &&
            entry['moniker'] == 'bootstrap/archivist')
        .toList();
    expect(result, hasLength(1));
    final archivistInspect = result[0];
    expect(
        archivistInspect['payload']['root']['fuchsia.inspect.Health']['status'],
        equals('OK'));
    // when a component's inspect times out, the SL4F snapshot facade waits up
    // to two minutes
  }, timeout: Timeout(Duration(minutes: 3)));
}
