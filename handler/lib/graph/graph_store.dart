// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:async';

import 'package:modular_core/uuid.dart';
import 'package:modular_core/graph/graph.dart';
import 'package:modular_core/graph/mem_graph.dart';

// Interface to Ledger.  This needs to be fleshed out; current only enough
// is added to support refactoring of existing functionality.
// TODO(jjsoh): Updat this interface based on bug #713
abstract class GraphStore {
  Future<Graph> createGraph(Uuid sessionId);
  Future<Graph> findGraph(Uuid sessionId);
}

// Thrown when createGraph() is called for a Graph that already exists.
class GraphAlreadyExists {
  final Uuid sessionId;
  final Future<Graph> existingGraph;
  GraphAlreadyExists(this.sessionId, this.existingGraph);
}

class GraphUnavailable {
  final Uuid sessionId;
  final String reason;
  GraphUnavailable(this.sessionId, this.reason);
}

// Convenient GraphStore for testing and other non-replicated use-cases.
class InMemoryGraphStore implements GraphStore {
  final Map<Uuid, Graph> _graphs = <Uuid, Graph>{};
  final GraphStorePrefixGenerator _prefixGenerator;

  InMemoryGraphStore({GraphStorePrefixGenerator prefixGenerator})
      : _prefixGenerator = prefixGenerator ?? ((Uuid id) => 'in_mem-$id');

  @override
  Future<Graph> createGraph(Uuid sessionId) {
    if (_graphs.containsKey(sessionId)) {
      throw new GraphAlreadyExists(
          sessionId, new Future.value(_graphs[sessionId]));
    }

    final MemGraph graph = new MemGraph(prefix: _prefixGenerator(sessionId));
    graph.metadata.debugName = 'InMemoryGraphStore';
    graph.metadata.debugTraceId = sessionId.toString();
    _graphs[sessionId] = graph;
    return new Future.value(graph);
  }

  @override
  Future<Graph> findGraph(Uuid sessionId) =>
      new Future<Graph>.value(_graphs[sessionId]);
}

// Used to generate non-default prefixes for graphs created by the GraphStore.
// Useful for debugging.
typedef String GraphStorePrefixGenerator(Uuid sessionId);
