// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// false positive, see https://github.com/dart-lang/linter/issues/1381
// ignore_for_file: close_sinks
import 'dart:async';
import 'package:fidl_fidl_test_protocoleventadd/fidl_async.dart' as fidllib;

// [START contents]
class Server extends fidllib.Example {
  final _onExistingEventStreamController = StreamController<void>();
  final _onNewEventStreamController = StreamController<void>();

  @override
  Stream<void> get onExistingEvent => _onExistingEventStreamController.stream;

  @override
  Stream<void> get onNewEvent => _onNewEventStreamController.stream;
}

void expectEvents(fidllib.ExampleProxy client) async {
  await client.onExistingEvent.first;
  await client.onNewEvent.first;
}
// [END contents]
