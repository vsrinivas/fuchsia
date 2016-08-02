// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:typed_data';

import 'package:modular/representation_types.dart';
import 'package:modular/builtin_types.dart';

class Rgb {
  int r;
  int g;
  int b;

  Rgb(this.r, this.g, this.b);

  Rgb.fromHsv(double h, double s, double v) {
    int i = (h * 6).floor();
    double f = h * 6 - i;
    double p = v * (1 - s);
    double q = v * (1 - f * s);
    double t = v * (1 - (1 - f) * s);
    double r, g, b;
    switch (i % 6) {
      case 0:
        r = v;
        g = t;
        b = p;
        break;
      case 1:
        r = q;
        g = v;
        b = p;
        break;
      case 2:
        r = p;
        g = v;
        b = t;
        break;
      case 3:
        r = p;
        g = q;
        b = v;
        break;
      case 4:
        r = t;
        g = p;
        b = v;
        break;
      case 5:
        r = v;
        g = p;
        b = q;
        break;
    }
    this.r = (r * 255).floor();
    this.g = (g * 255).floor();
    this.b = (b * 255).floor();
  }

  Rgb.fromInt(int i)
      : r = (i.toUnsigned(24) & 0xFF0000) >> 16,
        g = (i.toUnsigned(24) & 0xFF00) >> 8,
        b = (i.toUnsigned(24) & 0xFF);

  int toInt() => r << 16 | g << 8 | b;

  @override
  String toString() => "rgb($r, $g, $b)";
}

class RgbBindings implements RepresentationBindings<Rgb> {
  const RgbBindings();

  @override
  final String label =
      'https://github.com/domokit/modular/wiki/representation#rgb';

  @override
  Rgb decode(Uint8List data) => new Rgb.fromInt(BuiltinInt.read(data));

  @override
  Uint8List encode(Rgb value) => BuiltinInt.write(value.toInt());
}
