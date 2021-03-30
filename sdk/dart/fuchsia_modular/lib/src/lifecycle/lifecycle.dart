// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:fuchsia/fuchsia.dart' as fuchsia;
import 'package:fuchsia_services/services.dart';

import 'internal/_lifecycle_impl.dart';

/// Allows running components to be notified when lifecycle events happen in the
/// system.
abstract class Lifecycle {
  static Lifecycle? _singleton;

  /// Initializes the shared [Lifecycle] instance. This is required before
  /// Lifecycle can be constructed.
  static void enableLifecycleEvents(Outgoing outgoing) {
    if (_singleton == null) {
      _singleton = LifecycleImpl(
        outgoing: outgoing,
        exitHandler: fuchsia.exit,
      );
    } else {
      throw Exception(
          'Attempted to call Lifecycle.enableLifecycleEvents after it has already been enabled. '
          'Ensure that Lifecycle.enableLifecycleEvents is not called multiple times.');
    }
  }

  /// Obtains an instance of the shared [Lifecycle] instance.
  factory Lifecycle() {
    if (_singleton == null) {
      throw Exception(
          'Attempted to construct Lifecycle before lifecycle events have been enabled. '
          'Ensure that Lifecycle.enableLifecycleEvents is called first.');
    }
    return _singleton!;
  }

  /// Adds a terminate [listener] which will be called when the system starts to
  /// shutdown this process. Returns `false` if this listener was already added.
  bool addTerminateListener(Future<void> Function() listener);
}
