// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:async';

import 'package:modular/log.dart';
import 'package:modular/mojo_module.dart';
import 'package:modular/state_graph.dart';
import 'package:mojo/core.dart';

const String contestantLabel = "contestant";
const String winnerLabel = "winner";

class WinnerPickModule extends MojoModule {
  final Logger _log = log("WinnerPickModule");

  @override
  Future<Null> onChange(StateGraph state) async {
    _log.info("WinnerPick onChange");

    List<SemanticNode> contestants = state.root.getList([contestantLabel]);
    _log.info("WinnerPick matched ${contestants.length} contestants");

    // “So the last will be the first.” - set the last contestant as the winner.
    state.root.set([winnerLabel], contestants.last);
    await state.push();
  }
}

void main(List<String> args, Object handleToken) {
  new ModuleApplication(() {
    return new WinnerPickModule();
  }, new MojoHandle(handleToken));
}
