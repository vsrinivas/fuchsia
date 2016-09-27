// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:async';
import 'dart:developer';

/// More concise than calling a static method on [Timeline].
dynamic traceSync(String label, Function closure,
        {Map<String, String> arguments}) =>
    Timeline.timeSync(label, closure, arguments: arguments);

/// Async equivalent of Timeline.timeSync. See
/// https://github.com/dart-lang/sdk/issues/26201.
Future<dynamic> traceAsync(String label, Future<dynamic> closure(),
    {Map<String, String> arguments}) {
  final TimelineTask pushTask = new TimelineTask();
  pushTask.start(label, arguments: arguments);
  return closure().whenComplete(pushTask.finish);
}
