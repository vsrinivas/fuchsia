// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:flutter/material.dart';

import 'package:fuchsia_inspect/inspect.dart';

/// Returns [Rect] in global coordinates from the supplied [GlobalKey].
///
/// Returns [null] if associated RenderObject was never rendered.
Rect rectFromGlobalKey(GlobalKey key) {
  RenderBox box = key.currentContext?.findRenderObject();
  if (box != null && box.hasSize) {
    return MatrixUtils.transformRect(
      box.getTransformTo(null),
      Offset.zero & box.size,
    );
  }
  return null;
}

RelativeRect relativeRectFromGlobalKey(GlobalKey key) {
  final rect = rectFromGlobalKey(key);
  if (rect != null) {
    final screenRect = Offset.zero & MediaQuery.of(key.currentContext).size;
    return RelativeRect.fromRect(rect, screenRect);
  }
  return null;
}

/// Defines an interface to allow adding state to [Inspect] on demand.
abstract class Inspectable extends Object {
  void onInspect(Node node);
}
