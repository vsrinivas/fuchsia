// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:async';
import 'dart:math';

import 'package:modular/mojo_module.dart';
import 'package:modular/state_graph.dart';
import 'package:mojo/core.dart';
import 'package:representation_types/rgb.dart';

const String colorLabel = 'color';

class RandomColorPickerModule extends MojoModule {
  @override
  Future<Null> onChange(StateGraph state) {
    state.root.getOrDefault([colorLabel]).value =
        new Rgb.fromInt(new Random().nextInt(1 << 24));

    return state.push();
  }
}

void main(List<String> args, Object handleToken) {
  bindingsRegistry.register(Rgb, const RgbBindings());
  new ModuleApplication(() {
    return new RandomColorPickerModule();
  }, new MojoHandle(handleToken));
}
