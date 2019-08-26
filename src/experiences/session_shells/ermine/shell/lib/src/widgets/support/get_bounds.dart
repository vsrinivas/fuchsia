// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:ui';

import 'package:flutter/material.dart';

/// Defines a widget that allows retrieves its global bounds [Rect] post render.
class GetBounds extends StatelessWidget {
  /// The child to get the [Rect] for.
  final Widget child;

  /// Callback to receive the [Rect].
  final ValueChanged<Rect> onBounds;

  const GetBounds({@required this.child, this.onBounds});

  @override
  Widget build(BuildContext context) {
    WidgetsBinding.instance.addPostFrameCallback((_) {
      RenderBox box = context.findRenderObject();
      if (box != null && box.hasSize) {
        final offset = box.localToGlobal(Offset.zero);
        final size = box.size;
        onBounds?.call(offset & size);
      }
    });
    return child;
  }
}
