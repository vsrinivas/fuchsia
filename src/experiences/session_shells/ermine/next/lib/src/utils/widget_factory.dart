// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:flutter/widgets.dart';

/// A widget factory that allows creating mock widgets during tests.
class WidgetFactory {
  WidgetFactory._();

  @visibleForTesting
  static Widget Function(Type)? mockFactory;

  /// Returns the [Widget] returned by calling the supplied [fn]. If the
  /// [mockFactory] is set during tests, calls that instead.
  static Widget create<T extends Widget>(T Function() fn) {
    if (mockFactory != null) {
      return mockFactory!.call(T);
    }
    return fn();
  }
}
