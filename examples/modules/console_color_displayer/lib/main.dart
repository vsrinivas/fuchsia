// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:async';

import 'package:modular/mojo_module.dart';
import 'package:modular/state_graph.dart';
import 'package:mojo/core.dart';

const String colorLabel = 'color';

class ConsoleColorDisplayerModule extends MojoModule {
  @override // MojoModule
  Future<Null> onChange(StateGraph state) {
    final int color = state.root.get([colorLabel]).value;
    print("Display color: $color");
    return new Future<Null>.value();
  }
}

void main(List<String> args, Object handleToken) {
  new ModuleApplication(
      () => new ConsoleColorDisplayerModule(), new MojoHandle(handleToken));
}
