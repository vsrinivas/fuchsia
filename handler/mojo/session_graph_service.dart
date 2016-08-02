// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:async';

import 'package:handler/handler.dart';
import 'package:handler/graph/session_graph.dart';
import 'package:handler/session.dart';
import 'package:modular/graph/mojo/graph_server.dart';
import 'package:modular_core/log.dart';
import 'package:modular/modular/graph.mojom.dart' as mojo;
import 'package:modular/modular/handler.mojom.dart' as mojo;
import 'package:modular_core/uuid.dart';

/// Gives a mojo application the ability to obtain Graph handles for sessions
/// running in a [Handler] instance.
class SessionGraphServiceImpl extends mojo.SessionGraphService {
  final Handler _handler;

  static final Logger _log = log('handler.mojo.SessionGraphServiceImpl');

  SessionGraphServiceImpl(this._handler) {
    assert(_handler != null);
  }

  @override // mojo.SessionGraphService
  Future<Null> getGraph(String sessionId, mojo.GraphInterfaceRequest graphStub,
      void callback(mojo.HandlerStatus status)) async {
    final Session session =
        await _handler.findSession(Uuid.fromBase64(sessionId));
    if (session == null) {
      _log.severe('Session with ID $sessionId does not exist');
      callback(mojo.HandlerStatus.invalidSessionId);
    } else {
      graphStub.impl = new GraphServer(session.graph);
      callback(mojo.HandlerStatus.ok);
    }
  }
}
