// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:async';
import 'dart:convert';
import 'dart:math';
import 'dart:ui' as ui;

import 'package:flutter/material.dart';

const blackColor = Color(0xFF000000);
const grayColor = Color(0xFFBBBBBB);
const whiteColor = Color(0xFFFFFFFF);

const rgbColors = [
  Color(0xFFC08800),
  Color(0xFF88C088),
  Color(0xFFFF8888),
  Color(0xFF4088C0),
];

const String pngImageBase64 =
    'iVBORw0KGgoAAAANSUhEUgAAADIAAADIAgMAAADXWFpdAAAABGdBTUEAALGPC/xhBQAAAAFzUkdCAK7OHOkAAAAMUExURcCIAIjAiP+IiECIwOGm6dEAAAAtSURBVEjHY2AYBaNgiIJQJBAwyhvlDSHeKiSwYJQ3yhtCvP9I4MMob5Q3dHgAXXcJXJX7lj4AAAAASUVORK5CYII=';

const String jpgImageBase64 =
    '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';

void main() => runApp(Gamma());

class Gamma extends StatefulWidget {
  @override
  State<StatefulWidget> createState() {
    return GammaState();
  }
}

class GammaState extends State<Gamma> {
  bool _rotated;
  Timer _timer;
  ui.Image _pngImage;
  ui.Image _jpgImage;

  @override
  void initState() {
    super.initState();
    _rotated = false;
    _timer = Timer.periodic(const Duration(seconds: 5), _flipRotation);
    ui.decodeImageFromList(base64.decode(pngImageBase64), _setPngImage);
    ui.decodeImageFromList(base64.decode(jpgImageBase64), _setJpgImage);
  }

  void _flipRotation(Timer _timer) {
    setState(() {
      _rotated = !_rotated;
    });
  }

  void _setPngImage(ui.Image image) {
    setState(() {
      _pngImage = image;
    });
  }

  void _setJpgImage(ui.Image image) {
    setState(() {
      _jpgImage = image;
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
      painter: GammaPainter(_pngImage, _jpgImage, rotated: _rotated),
      child: Container(),
    );
  }
}

class GammaPainter extends CustomPainter {
  final bool rotated;
  final ui.Image pngImage;
  final ui.Image jpgImage;

  GammaPainter(this.pngImage, this.jpgImage, {@required this.rotated});

  @override
  void paint(Canvas canvas, Size size) {
    // Scale for pixel exact rendering.
    final dpr = ui.window.devicePixelRatio;
    canvas.scale(1.0 / dpr, 1.0 / dpr);

    var dims = Size(size.width * dpr, size.height * dpr);

    // Rotate 90 degrees.
    if (rotated) {
      dims = Size(size.height * dpr, size.width * dpr);
      canvas
        ..translate(dims.height, 0.0)
        ..rotate(90 * pi / 180);
    }

    var background = Offset.zero & dims;
    canvas.drawRect(background, Paint()..color = whiteColor);

    var x = 0.0;
    var y = 0.0;

    // Draw images in top left corner.
    if (pngImage != null) {
      canvas.drawImage(pngImage, Offset(x, 0.0), Paint());
    }
    x += 50;
    if (jpgImage != null) {
      canvas.drawImage(jpgImage, Offset(x, 0.0), Paint());
    }
    x += 50;

    const rgbSpanHeight = 50.0;
    const gradientHeight = 200.0;

    // RGB color spans that match images on the right.
    for (var i = 0; i < rgbColors.length; i++) {
      var span = Offset(x, y) & Size(dims.width - x, rgbSpanHeight);
      canvas.drawRect(span, Paint()..color = rgbColors[i]);
      y += rgbSpanHeight;
    }

    // Gray color with 50% brightness in center left.
    var left = Offset(0.0, y) &
        Size(dims.width / 2.0, dims.height - y - gradientHeight);
    canvas.drawRect(left, Paint()..color = grayColor);

    // Alternating black and white horizontal lines in center right.
    // This is our 50% brightness reference.
    while (y < dims.height - gradientHeight) {
      var line = Offset(dims.width / 2.0, y) & Size(dims.width / 2.0, 1.0);
      canvas.drawRect(line, Paint()..color = blackColor);
      y += 2.0;
    }

    // Gradient color span.
    var span = Offset(0.0, y) & Size(dims.width, dims.height - y);
    canvas.drawRect(
        span,
        Paint()
          ..shader = ui.Gradient.linear(
            span.centerLeft,
            span.centerRight,
            [
              grayColor,
              blackColor,
            ],
          ));
  }

  @override
  bool shouldRepaint(GammaPainter oldDelegate) => true;
}
