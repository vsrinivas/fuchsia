// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:typed_data';

import 'graph.dart';
import 'id.dart';
import 'ref.dart';

/// Sub-classes should override [state] and [nodeRefFactory].
abstract class BoundNodeRef extends Node {
  @override // Node
  final NodeId id;

  BoundNodeRef(this.id);

  GraphState get state;
  NodeRefFactory get nodeRefFactory;

  @override // Node
  bool get isDeleted => !state.containsNodeId(id);
  @override // Node
  Iterable<Edge> get outEdges {
    return state.outEdgeIds(id)?.map((EdgeId e) => new Edge(
            e,
            nodeRefFactory(state.origin(e)),
            nodeRefFactory(state.target(e)),
            state.labels(e))) ??
        [];
  }

  @override // Node
  Iterable<Edge> get inEdges {
    return state.inEdgeIds(id)?.map((EdgeId e) => new Edge(
            e,
            nodeRefFactory(state.origin(e)),
            nodeRefFactory(state.target(e)),
            state.labels(e))) ??
        [];
  }

  @override // Node
  Iterable<String> get valueKeys {
    return state.valueKeys(id);
  }

  @override // Node
  Uint8List getValue(String key) {
    return state.getValue(id, key);
  }

  @override // Node
  Iterable<Edge> outEdgesWithLabels(Iterable<String> labels) =>
      outEdges.where((Edge e) => labels.every(e.labels.contains));

  @override // Node
  Edge singleOutEdgeWithLabels(Iterable<String> labels) {
    final Iterable<Edge> edges = outEdgesWithLabels(labels);
    return edges.isEmpty ? null : edges.single;
  }

  @override // Node
  Iterable<Edge> inEdgesWithLabels(Iterable<String> labels) =>
      inEdges.where((Edge e) => labels.every(e.labels.contains));

  @override // Node
  Edge singleInEdgeWithLabels(Iterable<String> labels) {
    final Iterable<Edge> edges = inEdgesWithLabels(labels);
    return edges.isEmpty ? null : edges.single;
  }
}

class GraphBoundNodeRef extends BoundNodeRef {
  final Graph _graph;

  GraphBoundNodeRef(this._graph, NodeId id) : super(id);

  @override
  NodeRefFactory get nodeRefFactory => _graph.nodeRefFactory;
  @override
  GraphState get state => _graph.state;
}
