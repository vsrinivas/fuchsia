// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:async';

import 'package:modular/log.dart';
import 'package:modular/mojo_module.dart';
import 'package:modular/state_graph.dart';
import 'package:mojo/core.dart';
import 'package:representation_types/person.dart';

const String winnerLabel = "winner";

class WinnerAnnounceModule extends MojoModule {
  final Logger _log = log("WinnerAnnounceModule");

  @override
  void onInitialize() {
    bindingsRegistry.register(Person, const PersonBindings());
  }

  @override
  Future<Null> onChange(StateGraph state) {
    // Get all nodes with the "winner" semantic label.
    List<SemanticNode> winners = state.root.getList([winnerLabel]);

    // Iterate over all winners.
    for (final SemanticNode winner in winners) {
      _log.info("Aaand the winer is: ${winner.value}");
    }
    return new Future<Null>.value();
  }
}

void main(List<String> args, Object handleToken) {
  new ModuleApplication(() {
    return new WinnerAnnounceModule();
  }, new MojoHandle(handleToken));
}
