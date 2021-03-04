// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:async';

import 'package:fidl/fidl.dart';
import 'package:fidl_fuchsia_ui_input/fidl_async.dart';
import 'package:fidl_fuchsia_ui_policy/fidl_async.dart';
import 'package:fuchsia_services/services.dart';

/// Connects to [PointerCaptureListenerHack] service and makes the events
/// available as a [stream].
class PointerEventsStream implements PointerCaptureListenerHack {
  final Presentation presentation;
  final _binding = PointerCaptureListenerHackBinding();
  final _controller = StreamController<PointerEvent>();

  PointerEventsStream(this.presentation) {
    presentation.capturePointerEventsHack(_binding.wrap(this));
  }

  factory PointerEventsStream.withSvcPath() {
    final presentation = PresentationProxy();
    Incoming.fromSvcPath().connectToService(presentation);
    final pointerEventsStream = PointerEventsStream(presentation);
    presentation.ctrl.close();
    return pointerEventsStream;
  }

  Stream<PointerEvent> get stream => _controller.stream;

  void dispose() {
    _controller.close();
    _binding.close();
  }

  @override
  ServiceData get $serviceData => PointerCaptureListenerHackData();

  @override
  Future<void> onPointerEvent(PointerEvent event) async =>
      _controller.add(event);
}
