// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:math';

import 'package:flutter/material.dart';
import 'package:flutter/widgets.dart';
import 'package:modular_flutter/flutter_module.dart';
import 'package:representation_types/rgb.dart';

const String colorLabel = 'color';
const String cardLabel = 'card';

class ColorPickerState extends SimpleModularState {
  final GlobalKey<_ColorWheelState> colorWheelKey =
      new GlobalKey<_ColorWheelState>();

  @override // State
  Widget build(BuildContext context) => _buildWidget(context);

  Widget _buildWidget(BuildContext context) {
    return new Container(
        child: new Listener(
            child: new Center(child: new _ColorWheel(key: colorWheelKey)),
            onPointerDown: updateColor,
            onPointerMove: updateColor),
        constraints: const BoxConstraints.expand(),
        decoration: const BoxDecoration(backgroundColor: Colors.white));
  }

  void updateColor(PointerEvent event) {
    final RenderBox renderBox = context.findRenderObject();
    final Size windowSize = renderBox.size;
    final RenderBox colorWheel =
        colorWheelKey.currentContext.findRenderObject();
    final double x =
        event.position.x - (windowSize.width - colorWheel.size.width) / 2.0;
    final double y =
        event.position.y - (windowSize.height - colorWheel.size.height) / 2.0;
    final double radius =
        min(colorWheel.size.width, colorWheel.size.height) / 2.0;
    final Rgb rgb = xyToRgb(x, y, radius);
    if (rgb != null) {
      print("trying to write $rgb to graph");
      updateSession((SemanticNode session) {
        print("trying writing $rgb to graph");
        session.getOrDefault(const <String>[colorLabel]).value = rgb;
      });
    }
  }
}

class _ColorWheel extends StatefulWidget {
  final AssetImage colorWheel = new AssetImage("lib/assets/color_wheel.png");
  _ColorWheel({Key key}) : super(key: key);
  @override
  _ColorWheelState createState() => new _ColorWheelState();
}

class _ColorWheelState extends State<_ColorWheel> {
  @override
  Widget build(BuildContext context) => new Image(image: config.colorWheel);
}

Rgb xyToRgb(double x, double y, double radius) {
  double rx = x - radius;
  double ry = y - radius;
  double d = radius * radius;
  if (rx * rx + ry * ry > d) return null;
  double h = (atan2(ry, rx) + PI) / (2 * PI);
  double s = sqrt(d) / radius;
  return new Rgb.fromHsv(h, s, 1.0);
}

void main() {
  bindingsRegistry.register(Rgb, const RgbBindings());

  new FlutterModule.withState(() => new ColorPickerState());
}
