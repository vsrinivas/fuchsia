// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:modular_core/log.dart';
import 'package:logging/logging.dart';
import 'package:test/test.dart';

// Test the unit test portion of the logging system.
void main() {
  group('Log', () {
    final Logger _log = log("handler.SyncedStateImpl");
    List<LogRecord> _captureList;

    setUp(() => _captureList = startLogCapture(Level.WARNING));
    tearDown(() => stopLogCapture());

    test('Capture 1 line', () {
      _log.warning("Just a single line");
      expect(_captureList, hasLength(1));
      expect(_captureList[0].toString(), contains("single"));
    });

    test('Capture 2 lines', () {
      _log.warning("line 1");
      _log.warning("line 2");
      expect(_captureList, hasLength(2));
      expect(_captureList[1].toString(), contains("2"));
    });

    test('Capture Ignore Info', () {
      _log.info("line 1");
      _log.warning("line 2");
      expect(_captureList, hasLength(1));
      expect(_captureList[0].toString(), contains("2"));
    });
  });
}
