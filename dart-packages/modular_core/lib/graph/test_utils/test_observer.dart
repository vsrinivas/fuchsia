// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:async';
import 'dart:core';

import '../graph.dart';

class TestObserver {
  static const int kWaitTimeoutSeconds = 5;

  final List<Completer> _completers = [];

  int calls = 0;
  List<GraphEvent> events = [];
  GraphEvent get last => events.last;

  void call(GraphEvent event) {
    calls++;
    events.add(event);

    _completers.forEach((c) => c.complete());
    _completers.clear();
  }

  void clear() {
    calls = 0;
    events.clear();
  }

  Future<Null> waitForNextUpdate() {
    Completer completer = new Completer();
    _completers.add(completer);
    final Duration timeout = new Duration(seconds: kWaitTimeoutSeconds);
    return completer.future.timeout(timeout,
        onTimeout: () => throw new TimeoutException(
            'Timed out while waiting for TestObserver update', timeout));
  }
}
