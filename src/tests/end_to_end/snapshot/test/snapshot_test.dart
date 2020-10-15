// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:test/test.dart';
import 'package:sl4f/sl4f.dart' as sl4f;

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
  });
}
