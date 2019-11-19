// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:async';
import 'package:fidl_fuchsia_ui_remotewidgets/fidl_async.dart';

/// Defines a class that provides a [Stream] of UI [Spec].
///
/// The [stream] accessor provides the stream of [Spec] objects. The stream is
/// started by calling [listen]. Call [dispose] to stop listening.
/// Method [update] allows requesting a UI [Spec] by sending a [Value] as part
/// of the request to the underlying [QuickUi] proxy.
class UiStream {
  final QuickUi _ui;
  Spec _spec;
  StreamSubscription<Spec> _subscription;
  final _controller = StreamController<Spec>.broadcast();

  /// Constructor.
  UiStream(QuickUi ui) : _ui = ui;

  /// Returns the [Stream] over [Spec] objects.
  Stream<Spec> get stream => _controller.stream.asBroadcastStream();

  /// Returns the last [Spec] returned from QuickUi server.
  Spec get spec => _spec;

  /// Defines a 'null' spec, used by the service to signal the client to hide
  /// its UI.
  static final Spec nullSpec = Spec(title: null, groups: null);

  /// Start listening to the [QuickUi] server.
  void listen() {
    update(null);
  }

  /// Send a [Value] to QuickUi server to request a new [Spec].
  void update([Value value]) {
    _subscription?.cancel();
    _subscription = _ui.getSpec(value).asStream().listen((spec) {
      // Cache the spec until the next returned from the server.
      _spec = spec;
      if (spec != null) {
        _controller.add(spec);
      }
      listen();
    });
  }

  /// Stop listening to the [QuickUi] server.
  void dispose() {
    _subscription?.cancel();
  }
}
