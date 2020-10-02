// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This is an E2E test that often needs extra time to complete.
@Timeout(Duration(minutes: 10))

import 'package:test/test.dart';
import 'package:sl4f/sl4f.dart' as sl4f;
import 'util.dart';

const String allNode = 'core/archivist:root/all_archive_accessor:*';

const String feedbackNode = 'core/archivist:root/feedback_archive_accessor:*';

void main() {
  sl4f.Sl4f sl4fDriver;
  sl4f.Inspect inspect;

  group('archivist pipeline metrics', () {
    setUp(() async {
      sl4fDriver = sl4f.Sl4f.fromEnvironment();
      await sl4fDriver.startServer();
      inspect = sl4f.Inspect(sl4fDriver);
    });

    tearDown(() async {
      await sl4fDriver.stopServer();
      sl4fDriver.close();
    });

    test('archivist exposes metrics for "all" pipeline', () async {
      expect(await getInspectValues(inspect, allNode),
          multiValue(isNotNull, length: greaterThan(0)));
    });

    test('appmgr exposes metrics for "feedback" pipeline', () async {
      expect(await getInspectValues(inspect, feedbackNode),
          multiValue(isNotNull, length: greaterThan(0)));
    });
  }, timeout: Timeout(Duration(seconds: 60)));
}
