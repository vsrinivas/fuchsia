// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:flutter/widgets.dart';

class CirclePainter extends CustomPainter {
  final double radius;
  final double bottomOffset;

  CirclePainter({this.radius, this.bottomOffset});

  @override
  void paint(Canvas canvas, Size size) {
    Paint paint = new Paint()..color = new Color(0x8068EFAD);
    canvas.drawCircle(
        new Point(size.width / 2.0, size.height - bottomOffset), radius, paint);
  }

  @override
  bool shouldRepaint(CirclePainter oldPainter) =>
      radius != oldPainter.radius || bottomOffset != oldPainter.bottomOffset;

  @override
  bool hitTest(Point position) => false;
}
