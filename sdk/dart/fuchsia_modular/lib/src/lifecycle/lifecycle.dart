// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:fuchsia/fuchsia.dart' as fuchsia;

import 'internal/_lifecycle_impl.dart';

/// Allows running components to be notified when lifecycle events happen in the
/// system.
abstract class Lifecycle {
  static Lifecycle? _lifecycle;

  /// Initializes the shared [Lifecycle] instance
  factory Lifecycle() {
    return _lifecycle ??= LifecycleImpl(exitHandler: fuchsia.exit);
  }

  /// Adds a terminate [listener] which will be called when the system starts to
  /// shutdown this process. Returns `false` if this listener was already added.
  bool addTerminateListener(Future<void> Function() listener);
}
