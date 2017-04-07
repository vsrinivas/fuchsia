// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:flutter/widgets.dart';

/// Draws rounded corners with a [radius] and [color] via a [Decoration] which
/// can be used with [Container.foregroundDecoration] to simulate a [ClipRRect]
/// assuming the background color is solid [color].
class RoundedCornerDecoration extends Decoration {
  /// The radius of the drawn rounded corners.
  final double radius;

  /// The color of the drawn rounded corners.
  final Color color;

  /// Constructor.
  RoundedCornerDecoration({this.radius, this.color});

  @override
  BoxPainter createBoxPainter([VoidCallback onChanged]) =>
      new _RoundedCornerBoxPainter(radius: radius, color: color);

  @override
  bool hitTest(Size size, Point position) => false;
}

class _RoundedCornerBoxPainter extends BoxPainter {
  final double radius;
  final Color color;

  _RoundedCornerBoxPainter({this.radius, this.color});

  @override
  void paint(Canvas canvas, Offset offset, ImageConfiguration configuration) {
    Size size = configuration.size;

    canvas.drawDRRect(
      new RRect.fromLTRBXY(
        offset.dx,
        offset.dy,
        offset.dx + size.width,
        offset.dy + size.height,
        0.0,
        0.0,
      ),
      new RRect.fromLTRBXY(
        offset.dx,
        offset.dy,
        offset.dx + size.width,
        offset.dy + size.height,
        radius,
        radius,
      ),
      new Paint()..color = color,
    );
  }
}
