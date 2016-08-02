// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:flutter/material.dart';
import 'package:flutter/widgets.dart';
import 'package:modular_flutter/flutter_module.dart';
import 'package:representation_types/rgb.dart';

const String colorLabel = 'color';

class ColorDisplayerState extends SimpleModularState {
  Color get color {
    final Rgb rgb = session?.get(const <String>[colorLabel])?.value;
    if (rgb == null) {
      return Colors.white;
    }
    return new Color.fromARGB(0xFF, rgb.r, rgb.g, rgb.b);
  }

  @override // State
  Widget build(BuildContext context) => _buildWidget(context);

  Widget _buildWidget(BuildContext context) {
    Color displayedColor = this.color;
    final String titleText = runningAsPreview
        ? 'Display this color?'
        : '${_colorDescription(displayedColor)}';
    return new Container(
        child: new Center(child: new Text(titleText,
            style: Typography.black.title, textAlign: TextAlign.center)),
        decoration: new BoxDecoration(backgroundColor: displayedColor),
        constraints: const BoxConstraints.expand());
  }

  // The output of Color.toString() is too debuggy, so this returns a more
  // user-friendly description.
  String _colorDescription(final Color color) {
    return 'red: ${_intToHex(color.red)} green: ${_intToHex(color.green)} '
        'blue: ${_intToHex(color.blue)}';
  }

  String _intToHex(int i) => i.toRadixString(16).padLeft(2, '0');
}

void main() {
  bindingsRegistry.register(Rgb, const RgbBindings());

  new FlutterModule.withState(() => new ColorDisplayerState());
}
