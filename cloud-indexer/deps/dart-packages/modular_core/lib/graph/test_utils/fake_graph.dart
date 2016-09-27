// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:typed_data';

import '../graph.dart';
import '../graph_base.dart';
import '../query/query.dart';
import '../query/query_match_set.dart';

class FakeNode {
  final NodeId id;
  Map<String, List<int>> values = <String, List<int>>{};
  FakeNode(String strId) : id = new NodeId.fromString(strId);
}

class FakeEdge {
  final EdgeId id;
  final NodeId origin;
  final NodeId target;
  final List<String> labels;
  FakeEdge(String strId, dynamic origin, dynamic target, this.labels)
      : id = new EdgeId.fromString(strId),
        origin = new NodeId.fromString(origin.toString()),
        target = new NodeId.fromString(target.toString());
}

/// This class exposes graph inspection and traversal methods for a simple
/// static graph structure. Structure is specified by setting [fakeNodes] and
/// [fakeEdges] to appropriate values. All IDs are represented as Strings for
/// convenience in writing tests.
class FakeGraph extends GraphBase implements GraphState {
  List<FakeNode> fakeNodes = <FakeNode>[];
  List<FakeEdge> fakeEdges = <FakeEdge>[];

  FakeGraph({this.nodeIdGenerator, this.edgeIdGenerator});

  @override
  GraphState get state => this;

  @override
  NodeIdGenerator nodeIdGenerator;
  @override
  EdgeIdGenerator edgeIdGenerator;

  @override
  Iterable<NodeId> get nodeIds => fakeNodes.map((FakeNode n) => n.id);
  @override
  Iterable<EdgeId> get edgeIds => fakeEdges.map((FakeEdge e) => e.id);

  @override
  Iterable<EdgeId> outEdgeIds(NodeId id) => fakeEdges
      .where((FakeEdge e) => e.origin == id)
      .map((FakeEdge e) => e.id);
  @override
  Iterable<EdgeId> inEdgeIds(NodeId id) => fakeEdges
      .where((FakeEdge e) => e.target == id)
      .map((FakeEdge e) => e.id);

  @override
  Iterable<String> valueKeys(NodeId id) => _getNode(id)?.values?.keys;
  @override
  Uint8List getValue(NodeId id, String key) {
    final FakeNode node = _getNode(id);
    if (node == null || !node.values.containsKey(key)) return null;
    return new Uint8List.fromList(node.values[key]);
  }

  @override
  NodeId origin(EdgeId id) => _getEdge(id)?.origin;
  @override
  NodeId target(EdgeId id) => _getEdge(id)?.target;

  @override
  Iterable<String> labels(EdgeId id) =>
      fakeEdges.singleWhere((FakeEdge e) => e.id == id).labels;

  FakeNode _getNode(NodeId id) {
    final nodes = fakeNodes.where((FakeNode n) => n.id == id);
    assert(nodes.length < 2);
    return nodes.isEmpty ? null : nodes.first;
  }

  FakeEdge _getEdge(EdgeId id) {
    final edges = fakeEdges.where((FakeEdge e) => e.id == id);
    assert(edges.length < 2);
    return edges.isEmpty ? null : edges.first;
  }

  @override
  bool containsNodeId(NodeId id) => nodeIds.contains(id);
  @override
  bool containsEdgeId(EdgeId id) => edgeIds.contains(id);

  @override
  void addObserver(GraphChangeCallback c) {}
  @override
  void removeObserver(GraphChangeCallback c) {}
  @override
  void mutate(MutateGraphCallback c, {dynamic tag}) {}
  @override
  GraphQueryMatchSet query(final GraphQuery query) => null;
}
