// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:typed_data';

import 'graph.dart';
import 'mutation.dart';
import 'mutable_graph_state.dart';
import 'simple_graph.dart';

/// A Graph wrapper that lazily clones another Graph, and hides changes from
/// the underlying Graph by using a copy-on-write pattern.
///
/// This can be used to see what structural changes would take place when
/// mutations are applied to a Graph without those mutations being applied
/// directly.
///
/// Some caveats:
///   * Changes on the underlying Graph are blindly exposed.
///   * Observation is supported with some basic heuristics, s.t. events from
///     the base graph are masked if they wouldn't make sense given the state of
///     the lazy clone. Masked changes are still visible due to the previous
///     caveat.
///
/// [LazyClonedGraph] is meant to be short-lived, such that it does not have
/// the opportunity to see or observe changes to the base graph.
class LazyClonedGraph extends SimpleGraph {
  final Graph _base;

  LazyClonedGraph(final Graph base)
      : _base = base,
        super(new LazyClonedGraphState.forGraph(base), base.nodeIdGenerator,
            base.edgeIdGenerator) {}

  @override
  void addObserver(GraphChangeCallback callback) {
    if (super.observerCount == 0) {
      // Lazily register the observer so we don't receive events from the base
      // graph unnecessarily if we have no observers.
      _base.addObserver(_onBaseGraphChanged);
    }
    super.addObserver(callback); // GraphObservationMixin
  }

  @override
  void removeObserver(GraphChangeCallback callback) {
    super.removeObserver(callback); // GraphObservationMixin
    if (super.observerCount == 0) {
      // Remove our observer so we don't receive events from the base graph
      // unnecessarily if we have no observers.
      _base.removeObserver(_onBaseGraphChanged);
    }
  }

  // We observe the base graph to see which mutations to forward to our
  // observers, if any.
  void _onBaseGraphChanged(final GraphEvent event) {
    final Iterable<GraphMutation> baseMutations = event.mutations.coalesced;
    final List<GraphMutation> toNotify = [];

    final LazyClonedGraphState lazyCloneState = state as LazyClonedGraphState;

    // Mask mutations involving nodes and edges for which a lazy copy-on-write
    // clone exists. We mask an edge/node if it was removed in the clone but
    // still exists in the base.
    for (final GraphMutation m in baseMutations) {
      switch (m.type) {
        case GraphMutationType.addNode:
        case GraphMutationType.removeNode:
          if (lazyCloneState._nodes.containsKey(m.nodeId)) continue;
          break;
        case GraphMutationType.setValue:
          if (lazyCloneState.removedNodeIds.contains(m.nodeId) ||
              lazyCloneState.addedValueKeys(m.nodeId).contains(m.valueKey)) {
            continue;
          }
          break;

        // TODO(armansito): We don't report added/removed edges if that would
        // form an invalid edge in the lazy graph, HOWEVER, the
        // LazyClonedGraphState itself doesn't restrict that edge from getting
        // accessed. So I can have an edge in the base graph whose target and/or
        // origin node has been removed in the lazy cloned graph. Is this a bug
        // or a detail that doesn't matter?
        case GraphMutationType.addEdge:
        case GraphMutationType.removeEdge:
          if (lazyCloneState.removedNodeIds.contains(m.originNodeId) ||
              lazyCloneState.removedNodeIds.contains(m.targetNodeId) ||
              lazyCloneState._edges.containsKey(m.edgeId)) continue;
          break;
        default:
          break;
      }
      toNotify.add(m);
    }

    if (toNotify.isNotEmpty) notifyGraphChanged(toNotify);
  }
}

/// Implements [MutableGraphState] for use by [LazyClonedGraph], but may also
/// be used separately.
class LazyClonedGraphState implements MutableGraphState {
  final Graph _baseGraph;
  final GraphState _baseState;
  GraphState get baseState => _baseState ?? _baseGraph.state;

  LazyClonedGraphState(this._baseState) : _baseGraph = null;
  LazyClonedGraphState.forGraph(this._baseGraph) : _baseState = null;

  Map<NodeId, _NodeData> _nodes = <NodeId, _NodeData>{};
  Map<EdgeId, _EdgeData> _edges = <EdgeId, _EdgeData>{};

  Iterable<NodeId> get addedNodeIds =>
      _nodes.keys.where((id) => _nodes[id] != null);

  Iterable<NodeId> get removedNodeIds =>
      _nodes.keys.where((id) => _nodes[id] == null);

  Iterable<EdgeId> get addedEdgeIds =>
      _edges.keys.where((id) => _edges[id] != null);

  Iterable<EdgeId> get removedEdgeIds =>
      _edges.keys.where((id) => _edges[id] == null);

  Iterable<EdgeId> addedOutEdgeIds(NodeId nodeId) =>
      addedEdgeIds.where((edgeId) => _edges[edgeId].origin == nodeId);

  Iterable<EdgeId> addedInEdgeIds(NodeId nodeId) =>
      addedEdgeIds.where((edgeId) => _edges[edgeId].target == nodeId);

  Iterable<String> addedValueKeys(NodeId id) {
    if (!_nodes.containsKey(id)) return const <String>[];
    final values = _nodes[id].values;
    return values.keys.where((key) => values[key] != null);
  }

  Iterable<String> removedValueKeys(NodeId id) {
    if (!_nodes.containsKey(id)) return const <String>[];
    final values = _nodes[id].values;
    return values.keys.where((key) => values[key] == null);
  }

  @override // GraphState
  bool containsNodeId(NodeId id) =>
      _nodes[id] != null ||
      (!_nodes.containsKey(id) && baseState.containsNodeId(id));
  @override // GraphState
  bool containsEdgeId(EdgeId id) =>
      _edges[id] != null ||
      (!_edges.containsKey(id) && baseState.containsEdgeId(id));

  @override // GraphState
  Iterable<NodeId> get nodeIds => new Set<NodeId>.from(baseState.nodeIds)
    ..addAll(addedNodeIds)
    ..removeAll(removedNodeIds);

  @override // GraphState
  Iterable<EdgeId> get edgeIds => new Set<EdgeId>.from(baseState.edgeIds)
    ..addAll(addedEdgeIds)
    ..removeAll(removedEdgeIds);

  @override // GraphState
  Iterable<EdgeId> outEdgeIds(NodeId id) =>
      new Set<EdgeId>.from(baseState.outEdgeIds(id) ?? const <EdgeId>[])
        ..addAll(addedOutEdgeIds(id))
        ..removeAll(removedEdgeIds);

  @override // GraphState
  Iterable<EdgeId> inEdgeIds(NodeId id) =>
      new Set<EdgeId>.from(baseState.inEdgeIds(id) ?? const <EdgeId>[])
        ..addAll(addedInEdgeIds(id))
        ..removeAll(removedEdgeIds);

  @override // GraphState
  Iterable<String> valueKeys(NodeId id) {
    if (!containsNodeId(id)) return const <String>[];
    return new Set<String>()
      ..addAll(baseState.valueKeys(id) ?? [])
      ..addAll(addedValueKeys(id))
      ..removeAll(removedValueKeys(id));
  }

  @override // GraphState
  Uint8List getValue(NodeId id, String key) {
    if (_nodes.containsKey(id) && _nodes[id].values.containsKey(key)) {
      return _nodes[id].values[key];
    }
    return baseState.getValue(id, key);
  }

  @override // GraphState
  NodeId origin(EdgeId id) => _edges[id]?.origin ?? baseState.origin(id);
  @override // GraphState
  NodeId target(EdgeId id) => _edges[id]?.target ?? baseState.target(id);
  @override // GraphState
  Iterable<String> labels(EdgeId id) =>
      _edges[id]?.labels ?? baseState.labels(id);

  /// [MutableGraphState] says that subclasses may optionally perform validation
  /// and redundancy detection.  This implementation does neither.
  @override // MutableGraphState
  bool applyMutation(final GraphMutation mutation) {
    switch (mutation.type) {
      case GraphMutationType.addNode:
        _nodes[mutation.nodeId] = new _NodeData();
        break;
      case GraphMutationType.removeNode:
        _nodes[mutation.nodeId] = null;
        break;
      case GraphMutationType.addEdge:
        _edges[mutation.edgeId] = new _EdgeData(
            mutation.originNodeId, mutation.targetNodeId, mutation.labels);
        break;
      case GraphMutationType.removeEdge:
        _edges[mutation.edgeId] = null;
        break;
      case GraphMutationType.setValue:
        _nodes.putIfAbsent(mutation.nodeId, () => new _NodeData());
        _nodes[mutation.nodeId].values[mutation.valueKey] = mutation.newValue;
        break;
    }
    return true;
  }
}

class _EdgeData {
  List<String> labels;
  NodeId origin;
  NodeId target;
  _EdgeData(this.origin, this.target, this.labels);
}

class _NodeData {
  Map<String, Uint8List> values = <String, Uint8List>{};
}
