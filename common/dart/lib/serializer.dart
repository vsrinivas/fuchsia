// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:async';

/// Executes submitted closures serially one by one.
class Serializer {
  Future<Null> _serialized = new Future<Null>.value();

  /// Executes this closure after all previously submitted closures. The
  /// returned future will resolve once this closure is executed.
  Future<Null> execute(Function closure) {
    _serialized = _serialized.then((_) => closure());
    return _serialized;
  }
}
