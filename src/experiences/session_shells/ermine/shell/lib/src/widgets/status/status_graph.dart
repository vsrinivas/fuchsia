// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:flutter/material.dart';

const kStatusBackgroundColor = Color(0xFF0C0C0C);
const kStatusBorderColor = Color(0xFF262626);

class QuickGraph extends StatefulWidget {
  final double value;
  final int step;

  const QuickGraph({this.value, this.step});

  @override
  _QuickGraphState createState() => _QuickGraphState();
}

class _QuickGraphSample {
  double value;
  int step;
  _QuickGraphSample(this.value, this.step);
}

class _QuickGraphState extends State<QuickGraph> {
  final _data = <_QuickGraphSample>[];

  @override
  void initState() {
    super.initState();
    _data.add(_QuickGraphSample(widget.value, widget.step));
  }

  @override
  void didUpdateWidget(QuickGraph oldWidget) {
    super.didUpdateWidget(oldWidget);
    _data.add(_QuickGraphSample(widget.value, widget.step));
    if (_data.length > 100) {
      _data.removeAt(0);
    }
  }

  @override
  Widget build(BuildContext context) {
    return Container(
      height: 18,
      width: 130,
      decoration: BoxDecoration(
        color: kStatusBackgroundColor,
        border: Border.all(color: kStatusBorderColor),
      ),
      child: RepaintBoundary(
        child: CustomPaint(
          painter: _QuickGraphPainter(_data),
        ),
      ),
    );
  }
}

class _QuickGraphPainter extends CustomPainter {
  final List<_QuickGraphSample> data;

  const _QuickGraphPainter(this.data);

  @override
  void paint(Canvas canvas, Size size) {
    if (data.isEmpty) {
      return;
    }
    Paint paint = Paint()
      ..color = Colors.white
      ..style = PaintingStyle.fill
      ..strokeWidth = 1.0;

    final it = data.reversed;
    final path = Path();
    double x = size.width;
    double y = size.height;
    path.moveTo(x, y);

    for (final point in it) {
      x -= point.step;
      y = (1 - point.value) * size.height;
      if (x < 0) {
        break;
      }
      path.lineTo(x, y);
    }

    path
      ..lineTo(x, size.height)
      ..close();

    canvas.drawPath(path, paint);
  }

  @override
  bool shouldRepaint(CustomPainter oldDelegate) {
    return true;
  }
}
