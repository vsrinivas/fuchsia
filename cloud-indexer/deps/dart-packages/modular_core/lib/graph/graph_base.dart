// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'graph.dart';

/// A convenience base class that implements the basic node and edge retrieval
/// methods on [Graph] in terms of the [GraphState] available through
/// the [Graph.state] call.
abstract class GraphBase extends Graph {
  @override // Graph
  Iterable<Node> get nodes =>
      state.nodeIds.map((NodeId id) => nodeRefFactory(id));

  @override // Graph
  Iterable<Edge> get edges => state.edgeIds
      .map((EdgeId id) => new Edge.fromState(id, state, nodeRefFactory));

  @override // Graph
  Node node(NodeId id) => state.containsNodeId(id) ? nodeRefFactory(id) : null;

  @override // Graph
  Edge edge(EdgeId id) => state.containsEdgeId(id)
      ? new Edge.fromState(id, state, nodeRefFactory)
      : null;
}
