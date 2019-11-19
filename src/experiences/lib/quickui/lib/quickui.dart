// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:async';
import 'package:fidl_fuchsia_ui_remotewidgets/fidl_async.dart';

/// Defines an interface for building UI based on [QuickUi] protocol.
///
/// This class should be extended and the [update] method should be provided.
/// The [Spec] for the UI can be returned by setting the [spec] accessor. This
/// can be called any time to update the UI. If the user of this class provides
/// a [Value] at the time of requesting the UI, it is passed along in the
/// [update] method. If this result in a change of the UI, it can be returned
/// by calling the setter [spec].
abstract class UiSpec extends QuickUi {
  // The [Completer] that holds the future for an outstanding [getSpec] request.
  Completer<Spec> _completer = Completer<Spec>();

  // Constructor.
  UiSpec([Spec spec]) {
    if (spec != null) {
      _completer.complete(spec);
    }
  }

  /// Defines a 'null' spec, used to signal the QuickUI client to hide this
  /// service's UI.
  static final Spec nullSpec = Spec(title: null, groups: null);

  /// Completes any outstanding Get for [Spec].
  set spec(Spec value) {
    if (_completer.isCompleted) {
      _completer = Completer<Spec>();
    }
    _completer.complete(value);
  }

  /// Overridden by derived classes.
  void update(Value value);

  /// Overridden by derived classes.
  void dispose();

  @override
  Future<Spec> getSpec([Value value]) async {
    if (value != null) {
      update(value);
    }

    final future = _completer.future;
    if (_completer.isCompleted) {
      _completer = Completer<Spec>();
    }
    return future;
  }
}
