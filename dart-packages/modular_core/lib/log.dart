// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:logging/logging.dart';

export 'package:logging/logging.dart' show Logger;

/// Provide a preconfigured Logger instance.
///
/// To capture WARNING log messages for a unit test, use something like this:
/// (This code will also prevent log messages from displayubg during a test.)
/// Place these lines at the start of a group() function, otherwise they will
/// apply to all subsequent tests:
///    List<LogRecord> captureList;
///    setUp(() => captureList = startLogCapture(WARNING));
///    tearDown(() => stopLogCapture());
/// Within a test case:
///    expect(captureList, isEmpty);
/// or:
///    expect(captureList[0], contains("missing"));

typedef Logger LoggerFactory(String name);

LoggerFactory _loggerFactory = (String name) => new Logger(name);

List<LogRecord> _captureList;
Level _lastLevel;

/// For unit testing the production of messages.
List<LogRecord> startLogCapture(Level value) {
  assert(_captureList == null);
  _lastLevel = Logger.root.level;
  Logger.root.level = value;
  _captureList = [];
  Logger.root.clearListeners();
  Logger.root.onRecord.listen(_captureList.add);
  return _captureList;
}

/// For unit testing the production of messages.
void stopLogCapture() {
  if (_captureList == null) return;
  Logger.root.clearListeners();
  Logger.root.onRecord.listen(_recordPrinter);
  Logger.root.level = _lastLevel;
  _captureList = null;
}

void _recordPrinter(LogRecord rec) =>
    // Print only the time, not the date.
    print('${rec.time.toIso8601String().substring(11, 23)} '
        '${rec.loggerName} ${rec.level.name} ${rec.message}');

final LoggerFactory log = () {
  Logger.root.level = Level.INFO;
  Logger.root.onRecord.listen(_recordPrinter);
  return _loggerFactory;
}();
