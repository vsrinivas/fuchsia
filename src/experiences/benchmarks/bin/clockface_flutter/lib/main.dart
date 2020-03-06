// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:async';
import 'dart:math';

import 'package:flutter/material.dart';

const backgroundColor = Color(0xFFEBD4B3);
const hourHandColor = Color(0xFFFD4763);
const minuteHandColor = Color(0x7FFEA3B1);
const secondHandColor = Colors.white;
const shadowColor = Color(0x0D000000);
const radius = 0.4;
const thickness = radius / 20.0;
const offset = radius / 5.0;
const elevation = 0.01;

void main() => runApp(ClockFace());

class ClockFace extends StatefulWidget {
  @override
  State<StatefulWidget> createState() {
    return ClockFaceState();
  }
}

class ClockFaceState extends State<ClockFace> {
  DateTime _now;
  Timer _timer;

  @override
  void initState() {
    super.initState();
    _now = DateTime.now().toUtc();
    _timer = Timer.periodic(const Duration(milliseconds: 16), _setNow);
  }

  void _setNow(Timer _timer) {
    setState(() {
      _now = DateTime.now().toUtc();
    });
  }

  @override
  void dispose() {
    _timer.cancel();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    return CustomPaint(
      painter: ClockPainter(_now),
      child: Container(),
    );
  }
}

class Hand {
  RRect _hand;
  RRect _shadow;
  final Color _color;
  final Point _pos;
  final double _angle;

  Hand(this._pos, double thickness, double length, double offset, this._color,
      double shadowOffset, this._angle) {
    _hand = RRect.fromRectAndRadius(
        Rect.fromLTWH(_pos.x - (thickness / 2.0 + offset),
            _pos.y - thickness / 2.0, length, thickness),
        Radius.circular(thickness / 2.0));
    _shadow = RRect.fromRectAndRadius(
        _hand.outerRect.translate(shadowOffset, shadowOffset * 2.0),
        _hand.tlRadius);
  }

  void paintHand(Canvas canvas) {
    canvas
      ..save()
      ..translate(_pos.x, _pos.y)
      ..rotate(_angle)
      ..translate(-_pos.x, -_pos.y)
      ..drawRRect(_hand, Paint()..color = _color)
      ..restore();
  }

  Path shadowPath() {
    var path = Path()..addRRect(_shadow);

    var matrix = Matrix4.identity()
      ..translate(_pos.x, _pos.y)
      ..rotateZ(_angle)
      ..translate(-_pos.x, -_pos.y);

    return path.transform(matrix.storage);
  }
}

class ClockPainter extends CustomPainter {
  final DateTime _now;

  ClockPainter(this._now);

  double ratioToAngle(double ratio, double total) {
    const rewind = -0.25; // account for 3 to 12 o'clock rewinding
    return ((rewind + ratio / total) % 1.0) * 2.0 * pi;
  }

  @override
  void paint(Canvas canvas, Size size) {
    var background = Offset.zero & size;
    canvas.drawRect(background, Paint()..color = backgroundColor);

    var scale = min(size.width, size.height);
    var center = Point(size.width / 2.0, size.height / 2.0);
    var second =
        (_now.second + 1).toDouble() + _now.millisecond.toDouble() / 1e+3;
    var minute = (_now.minute + 1).toDouble() + second / 60.0;
    var hour = (_now.hour % 12 + 1).toDouble() + minute / 60.0;

    var hourAngle = ratioToAngle(hour, 12.0);
    var minuteAngle = ratioToAngle(minute, 60.0);
    var secondAngle = ratioToAngle(second, 60.0);

    var secondHand = Hand(
        center,
        scale * thickness / 2.0,
        scale * (radius + offset),
        scale * offset,
        secondHandColor,
        scale * elevation,
        secondAngle);
    var minuteHand = Hand(center, scale * thickness, scale * radius, 0.0,
        minuteHandColor, scale * elevation, minuteAngle);
    var hourHand = Hand(center, scale * thickness * 2.0, scale * radius,
        scale * offset, hourHandColor, scale * elevation, hourAngle);

    var shadowHourSecond = Path.combine(
        PathOperation.union, hourHand.shadowPath(), secondHand.shadowPath());
    var shadowAll = Path.combine(
        PathOperation.union, shadowHourSecond, minuteHand.shadowPath());

    canvas
      ..drawPath(shadowHourSecond, Paint()..color = shadowColor)
      ..drawPath(shadowAll, Paint()..color = shadowColor);

    secondHand.paintHand(canvas);
    minuteHand.paintHand(canvas);
    hourHand.paintHand(canvas);
  }

  @override
  bool shouldRepaint(ClockPainter oldDelegate) => true;
}
