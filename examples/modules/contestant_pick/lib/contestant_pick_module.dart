// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:async';

import 'package:modular/log.dart';
import 'package:modular/mojo_module.dart';
import 'package:modular/state_graph.dart';
import 'package:mojo/core.dart' as core;
import 'package:representation_types/person.dart';

const String contestantLabel = "contestant";

class ContestantPickModule extends MojoModule {
  final Logger _log = log("ContestantPickModule");

  @override
  Future<Null> onChange(StateGraph state) async {
    state.root.create([contestantLabel]).value = new Person()
      ..name = "Alice"
      ..email = "alice@example.com";
    state.root.create([contestantLabel]).value = new Person()
      ..name = "Bob"
      ..email = "bob@example.com";
    state.root.create([contestantLabel]).value = new Person()
      ..name = "Charlie"
      ..email = "charlie@example.com";
    await state.push();

    _log.info("Created three contestants.");
  }
}

void main(List<String> args, Object handleToken) {
  bindingsRegistry.register(Person, const PersonBindings());

  new ModuleApplication(() {
    return new ContestantPickModule();
  }, new core.MojoHandle(handleToken));
}
