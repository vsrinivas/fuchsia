// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:typed_data';

import 'graph.dart';
import 'lazy_cloned_graph.dart' show LazyClonedGraphState;
import 'mutable_graph_state.dart';
import 'mutation.dart';
import 'id.dart';
import 'ref.dart';
import 'validating_lazy_cloned_graph_state.dart';

typedef void EachMutationCallback(GraphMutation mutation);

/// An implementation of [GraphMutator] that buffers a list of [GraphMutation]
/// objects that result from calls made to this class.
///
/// [BufferingMutator] performs validation and will throw an [ArgumentError] in
/// the case of failed validation.
///
/// The validation guarantees that if [nodeIdGenerator] and [edgeIdGenerator] do
/// not produce ID collisions and [mutations] are applied immediately to the
/// [Graph] passed in for state reference, they will succeed without error.
class BufferingMutator implements GraphMutator {
  /// A list of [GraphMutation] objects built as a side-effect of mutation calls
  /// below.
  final List<GraphMutation> _mutations = <GraphMutation>[];

  final Graph _graph;
  final MutableGraphState _tempState;

  final NodeRefFactory _nodeRefFactory;

  final EachMutationCallback onEachMutation;

  /// [_graph] is used to read current graph state when building mutations that
  /// require knowledge of past state (setValue, removeNode, removeEdge).
  ///
  /// If [validating] is true, then each mutation that gets applied through this
  /// mutator will be validated against a temporary copy of [graph] and an
  /// exception will be thrown for each mutation that doesn't apply cleanly.
  /// Otherwise, no validation will be performed until all buffered mutations
  /// get applied to the graph. This is useful for client code that wants to
  /// create and use a BufferingMutator directly as a simple container to
  /// generate and store mutations.
  BufferingMutator(Graph graph,
      {this.onEachMutation,
      NodeRefFactory nodeRefFactory,
      bool allowRedundantMutations: false,
      bool validating: true})
      : _graph = graph,
        _nodeRefFactory = nodeRefFactory ?? graph.nodeRefFactory,
        _tempState = validating
            ? new ValidatingLazyClonedGraphState.forGraph(graph,
                allowRedundantMutations: allowRedundantMutations)
            : new LazyClonedGraphState.forGraph(graph);

  Iterable<GraphMutation> get mutations =>
      new List<GraphMutation>.unmodifiable(_mutations);

  @override // GraphMutator
  Node addNode() {
    final NodeId nodeId = _graph.nodeIdGenerator();
    apply(new GraphMutation.addNode(nodeId));
    return _nodeRefFactory(nodeId);
  }

  @override // GraphMutator
  void removeNode(NodeId nodeId) {
    // Find all the in-edges and out-edges that exist (either in the graph
    // state or in mutations) so that we can remove them.
    Set<EdgeId> inOutEdges = new Set<EdgeId>();
    inOutEdges.addAll(_tempState.inEdgeIds(nodeId));
    inOutEdges.addAll(_tempState.outEdgeIds(nodeId));
    inOutEdges.forEach(removeEdge);
    apply(new GraphMutation.removeNode(nodeId));
  }

  @override // GraphMutator
  void setValue(NodeId nodeId, String key, Uint8List value) {
    apply(new GraphMutation.setValue(nodeId, key, value));
  }

  @override // GraphMutator
  Edge addEdge(NodeId originId, Iterable<String> labels, [NodeId targetId]) {
    if (targetId == null) targetId = addNode().id;

    final EdgeId edgeId = _graph.edgeIdGenerator();
    apply(new GraphMutation.addEdge(edgeId, originId, targetId, labels));

    return new Edge(
        edgeId, _nodeRefFactory(originId), _nodeRefFactory(targetId), labels);
  }

  @override // GraphMutator
  void removeEdge(EdgeId edgeId) {
    if (!_tempState.containsEdgeId(edgeId)) {
      throw new FailedGraphMutation(null,
          errorString: 'Attempt to remove edgeId $edgeId that does not exist.');
    }
    apply(new GraphMutation.removeEdge(edgeId, _tempState.origin(edgeId),
        _tempState.target(edgeId), _tempState.labels(edgeId)));
  }

  @override // GraphMutator
  void apply(GraphMutation mutation) {
    if (!_tempState.applyMutation(mutation)) return;
    _mutations.add(mutation);
    if (onEachMutation != null) onEachMutation(mutation);
  }
}
