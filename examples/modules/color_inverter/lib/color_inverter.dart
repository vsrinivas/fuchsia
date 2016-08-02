// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:async';

import 'package:modular/mojo_module.dart';
import 'package:modular/state_graph.dart';
import 'package:mojo/core.dart';
import 'package:representation_types/rgb.dart';

const String invertedLabel = 'inverted';
const String colorLabel = 'color';

class ColorInverterModule extends MojoModule {
  @override // MojoModule
  Future<Null> onChange(StateGraph state) {
    final Rgb colorValue = state.root.get([colorLabel]).value;
    final Rgb invertedColor = new Rgb.fromInt(-1 - colorValue.toInt());
    state.root.getOrDefault([invertedLabel, colorLabel]).value = invertedColor;
    print("Inverted color is : $invertedColor");

    return state.push();
  }
}

void main(List<String> args, Object handleToken) {
  bindingsRegistry.register(Rgb, const RgbBindings());

  new ModuleApplication(() {
    return new ColorInverterModule();
  }, new MojoHandle(handleToken));
}
