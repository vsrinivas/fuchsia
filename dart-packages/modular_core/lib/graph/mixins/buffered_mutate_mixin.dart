// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import '../graph.dart';
import '../ref.dart';
import '../bound_ref.dart';
import '../buffering_mutator.dart';
import '../lazy_cloned_graph.dart';

/// Provides an implementation for [mutate()] using the [BufferingMutator]
/// utility class, and forwards all mutations to [applyMutations()].
abstract class BufferedMutateMixin {
  Graph get mixinGraph;

  /// Applies the mutations in [mutations] to the Graph. The mutations are
  /// guaranteed to be valid when produced through [mutate()], since
  /// [BufferedMutator] performs its own validation.
  ///
  /// However, in the case of additional validation errors or other errors, the
  /// graph state must be left as it was before [applyMutations()] was called.
  ///
  /// Any client-recoverable errors should throw a variant of
  /// [FailedGraphMutation].
  void applyMutations(List<GraphMutation> mutations, {dynamic tag});

  void mutate(MutateGraphCallback fn, {dynamic tag}) {
    // Keep a local copy-on-write view of the Graph as mutations are applied.
    LazyClonedGraph appliedView = new LazyClonedGraph(mixinGraph);
    // The [_GraphProvider] allows us to
    _GraphProvider graphProvider = new _GraphProvider(appliedView);

    BufferingMutator mutator = new BufferingMutator(mixinGraph,
        onEachMutation: (GraphMutation m) => appliedView.applyMutations([m]),
        nodeRefFactory: (NodeId id) => new _ProxyNodeRef(id, graphProvider));

    fn(mutator);

    // Short-circuit if we don't have anything to apply.
    if (mutator.mutations.isEmpty) return;

    // Switch all the refs over to viewing the "live" graph, and not our local
    // copy.
    graphProvider.graph = mixinGraph;

    applyMutations(mutator.mutations.toList(), tag: tag);
  }
}

/// These classes define Refs that give us control to change the [Graph] they
/// reference once they have already been created.
class _GraphProvider {
  Graph graph;
  _GraphProvider(this.graph);
}

class _ProxyNodeRef extends BoundNodeRef {
  _GraphProvider _graphProvider;

  _ProxyNodeRef(NodeId id, this._graphProvider) : super(id);

  @override
  GraphState get state => _graphProvider.graph.state;
  @override
  NodeRefFactory get nodeRefFactory =>
      (NodeId id) => new _ProxyNodeRef(id, _graphProvider);
}
