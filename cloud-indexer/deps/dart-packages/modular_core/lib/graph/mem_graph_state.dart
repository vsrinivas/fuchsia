// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:typed_data';

import 'id.dart';
import 'mutable_graph_state.dart';
import 'mutation.dart';

/// A basic in-memory implementation of [GraphState]. Implementations can use
/// this to store/cache Node/Edge data.
class MemGraphState implements MutableGraphState {
  final Map<NodeId, _NodeData> _nodes = <NodeId, _NodeData>{};
  final Map<EdgeId, _EdgeData> _edges = <EdgeId, _EdgeData>{};
  final Map<NodeId, List<EdgeId>> _edgesTo = <NodeId, List<EdgeId>>{};
  final Map<NodeId, List<EdgeId>> _edgesFrom = <NodeId, List<EdgeId>>{};

  MemGraphState clone() {
    MemGraphState newState = new MemGraphState();
    _nodes.forEach((final NodeId key, final _NodeData value) {
      newState._nodes[key] = new _NodeData.from(value);
    });
    _edges.forEach((final EdgeId key, final _EdgeData value) {
      newState._edges[key] = new _EdgeData.from(value);
    });
    newState._edgesTo.addAll(_edgesTo);
    newState._edgesFrom.addAll(_edgesFrom);
    return newState;
  }

  /// [MutableGraphState] says that subclasses may optionally perform validation
  /// and redundancy detection.  This implementation does neither.
  @override // MutableGraphState
  bool applyMutation(final GraphMutation mutation) {
    switch (mutation.type) {
      case GraphMutationType.addNode:
        _nodes[mutation.nodeId] = new _NodeData();
        _edgesTo[mutation.nodeId] = [];
        _edgesFrom[mutation.nodeId] = [];
        break;
      case GraphMutationType.removeNode:
        _nodes.remove(mutation.nodeId);
        _edgesTo.remove(mutation.nodeId);
        _edgesFrom.remove(mutation.nodeId);
        break;
      case GraphMutationType.addEdge:
        _edges[mutation.edgeId] = new _EdgeData(mutation.originNodeId,
            mutation.targetNodeId, mutation.labels.toList());
        // Make it easy to find this edge from origin.outEdges and target.inEdges.
        _edgesFrom[mutation.originNodeId].add(mutation.edgeId);
        _edgesTo[mutation.targetNodeId].add(mutation.edgeId);
        break;
      case GraphMutationType.removeEdge:
        final _EdgeData edgeData = _edges[mutation.edgeId];
        _edgesTo[edgeData.target].remove(mutation.edgeId);
        _edgesFrom[edgeData.origin].remove(mutation.edgeId);
        _edges.remove(mutation.edgeId);
        break;
      case GraphMutationType.setValue:
        if (mutation.newValue == null)
          _nodes[mutation.nodeId].values.remove(mutation.valueKey);
        else
          _nodes[mutation.nodeId].values[mutation.valueKey] = mutation.newValue;
        break;
    }
    return true;
  }

  @override // GraphState
  bool containsNodeId(NodeId id) => _nodes.containsKey(id);

  @override // GraphState
  bool containsEdgeId(EdgeId id) => _edges.containsKey(id);

  @override // GraphState
  Iterable<NodeId> get nodeIds => _nodes.keys;

  @override // GraphState
  Iterable<EdgeId> get edgeIds => _edges.keys;

  @override // GraphState
  Iterable<EdgeId> outEdgeIds(NodeId id) => _edgesFrom[id];

  @override // GraphState
  Iterable<EdgeId> inEdgeIds(NodeId id) => _edgesTo[id];

  @override // GraphState
  Iterable<String> valueKeys(NodeId id) => _nodes[id]?.values?.keys;

  @override // GraphState
  Uint8List getValue(NodeId id, String key) => _nodes[id]?.getValue(key);

  @override // GraphState
  NodeId origin(EdgeId id) => _edges[id]?.origin;

  @override // GraphState
  NodeId target(EdgeId id) => _edges[id]?.target;

  @override // GraphState
  Iterable<String> labels(EdgeId id) => _edges[id]?.labels;
}

class _EdgeData {
  List<String> labels;
  NodeId origin;
  NodeId target;
  _EdgeData(this.origin, this.target, this.labels);
  _EdgeData.from(final _EdgeData other) {
    labels = new List<String>.from(other.labels);
    origin = other.origin;
    target = other.target;
  }
}

class _NodeData {
  Map<String, Uint8List> values = <String, Uint8List>{};
  _NodeData();
  _NodeData.from(final _NodeData other) {
    values = new Map<String, Uint8List>.from(other.values);
  }

  Uint8List getValue(String key) => values[key];
}
