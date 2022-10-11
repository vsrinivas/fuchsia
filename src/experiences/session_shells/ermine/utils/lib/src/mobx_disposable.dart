// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:meta/meta.dart';
import 'package:mobx/mobx.dart';

/// Defines a mixin for disposing MobX reactions during cleanup.
///
/// The client code adds the disposer functions to [reactions]. All the
/// functions in it are called during [dispose].
mixin Disposable {
  /// Holds the disposer functions for MobX reactions.
  final reactions = <ReactionDisposer>[];

  @mustCallSuper
  void dispose() {
    for (final reaction in reactions) {
      reaction();
    }
    reactions.clear();
  }
}
