// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:handler/bindings.dart';
import 'package:handler/module_instance.dart';
import 'package:modular/builtin_types.dart';
import 'package:modular_core/graph/graph.dart';
import 'package:modular_core/graph/lazy_cloned_graph.dart';
import 'package:modular_core/graph/mem_graph.dart';
import 'package:modular/graph/mojo/graph_server.dart';
import 'package:modular/graph/mojo/mutation_utils.dart';
import 'package:modular_core/graph/mutation.dart';
import 'package:modular_core/graph/ref.dart';
import 'package:modular_core/log.dart';
import 'package:modular/modular/graph.mojom.dart' as mojom;
import 'package:parser/expression.dart';

import 'module_runner.dart';

/// Hosts the footprint graph for a running module over mojo.
class ModuleFootprintGraphServer extends GraphServer {
  final Logger _log = log('handler.ModuleFootprintGraphServer');

  /// The module instance whose footprint graph is hosted by us.
  final ModuleInstance _instance;

  /// The mojo module runner for the instance.
  final MojoModuleRunner _runner;

  // The list of [GraphMutation]s that we should send to the modules. We don't
  // send them as they come, instead we notify the modules explicitly when the
  // ModuleRunner tells us to in [updateModuleGraph()].
  bool _needsUpdate = false;
  final List<GraphMutation> _pendingMutations = <GraphMutation>[];

  ModuleFootprintGraphServer(final ModuleInstance instance, this._runner)
      : _instance = instance,
        super(_createFootprintGraph(instance)) {
    assert(_instance != null);

    // We observe the entire session graph and explicitly cherry pick the
    // mutations that get sent to the module instance based on its latest
    // footprint.
    _instance.session.graph.addObserver(_onSessionGraphChanged);
  }

  /// Tells the server to apply pending [GraphMutation]s to the module's
  /// footprint graph, notifying the module instance in the process.
  void updateModuleGraph() {
    _needsUpdate = true;
    _maybeFlushPendingMutations();
  }

  void _maybeFlushPendingMutations() {
    if (_needsUpdate && _pendingMutations.isNotEmpty) {
      // This will notify the observers of the footprint graph over mojo.
      super.hostedGraph.mutate(
          (final GraphMutator gm) => _pendingMutations.forEach(gm.apply));
      _needsUpdate = false;
      _pendingMutations.clear();
    }
  }

  @override // GraphServer
  void applyMutations(final List<mojom.GraphMutation> mojomMutations,
      void callback(mojom.GraphStatus status, String errorDescription)) {
    if (mojomMutations.isEmpty) {
      callback(mojom.GraphStatus.success, null);
      return;
    }
    try {
      final List<GraphMutation> baseMutations =
          mojomMutations.map(mojomMutationToDart).toList();
      _adjustMutations(baseMutations);

      final List<GraphMutation> finalMutations = <GraphMutation>[]
        ..addAll(baseMutations);
      for (final GraphMutation m in baseMutations) {
        finalMutations.addAll(_instance.binding.updateOutputMutations(m));
      }

      // This modifies the session graph. Changes will be propagated to the
      // footprint graph when we receive _onSessionGraphChanged below.
      _instance.session.updateOutput(_instance, finalMutations);
      _runner.composeDisplayOutput();

      callback(mojom.GraphStatus.success, null);
    } catch (e, stackTrace) {
      callback(mojom.GraphStatus.failure, '$e\n$stackTrace');
    }
  }

  // Creates the sub-graph of the session graph that forms the footprint of
  // |instance|. We use the same Node and edge IDs to provide a view for
  // reading.
  /// TODO(mesch): Filter module graph labels down to manifest labels in module
  /// runner.
  static Graph _createFootprintGraph(final ModuleInstance instance) {
    final Set<Node> nodes = instance.footprintNodes.toSet();
    final Set<Edge> edges = instance.footprintEdges.toSet();

    // This graph won't automatically reflect changes from the session graph. We
    // do that manually in the observer callback, since we need to filter
    // mutations that don't belong in the footprint.
    final MemGraph footprintGraph = new MemGraph();
    footprintGraph.mutate((final GraphMutator mutator) {
      for (final Node n in nodes) {
        mutator.apply(new GraphMutation.addNode(n.id));
        for (final String valueKey in n.valueKeys) {
          mutator.setValue(n.id, valueKey, n.getValue(valueKey));
        }
      }
      for (final Edge e in edges) {
        mutator.apply(new GraphMutation.addEdge(
            e.id, e.origin.id, e.target.id, e.labels));
      }
    });
    return footprintGraph;
  }

  void _onSessionGraphChanged(final GraphEvent event) {
    // Forward the relevant mutations to the footprint graph by filtering out
    // those that don't belong there.
    // TODO(armansito): We have to find a way to do this more efficiently.
    // Iterating through mutations over the entire session graph for each module
    // instance for filtering won't scale.
    final Set<NodeId> nodeIds =
        _instance.footprintNodes.map((final Node n) => n.id).toSet();
    final Set<EdgeId> edgeIds =
        _instance.footprintEdges.map((final Edge e) => e.id).toSet();
    final filteredMutations = <GraphMutation>[];
    for (final GraphMutation mutation in event.mutations.coalesced) {
      switch (mutation.type) {
        // For edge and node removals, we first check that the node or edge in
        // question is contained in the hosted graph we want to update. We check
        // [hostedGraph] because [nodeIds] and [edgeIds] above already has these
        // removed.
        case GraphMutationType.removeNode:
          if (!super.hostedGraph.state.containsNodeId(mutation.nodeId)) {
            continue;
          }
          break;
        case GraphMutationType.removeEdge:
          if (!super.hostedGraph.state.containsEdgeId(mutation.edgeId)) {
            continue;
          }
          break;
        // For others we look at the nodes/edges from the ModuleInstance.
        case GraphMutationType.addNode:
        case GraphMutationType.setValue:
          if (!nodeIds.contains(mutation.nodeId)) continue;
          break;
        case GraphMutationType.addEdge:
          if (!edgeIds.contains(mutation.edgeId)) continue;
          break;
      }
      filteredMutations.add(mutation);
    }

    if (filteredMutations.isEmpty) return;

    _pendingMutations.addAll(filteredMutations);
    _maybeFlushPendingMutations();
  }

  /// Adjusts the mutation stream:
  ///
  /// 1. When adding a display edge, write a special value of type
  ///    Binding.displayNodeLabel = 'true'.
  ///
  /// 2. When adding an edge, change its labels from what is specified in the
  ///    Module's manifest to those that were specified in the recipe.
  ///
  void _adjustMutations(final List<GraphMutation> mutations) {
    // We need to be able to traverse the Graph post-mutations so that we can
    // infer about matching PathExprs, but we want to do this so that we can
    // rewrite some of the mutations. This allows us to do that.
    final LazyClonedGraph appliedMutationsGraph =
        new LazyClonedGraph(_instance.session.graph);
    appliedMutationsGraph.mutate((final GraphMutator mutator) {
      mutations.forEach(mutator.apply);
    });

    final List<GraphMutation> extra = <GraphMutation>[];
    for (int i = 0; i < mutations.length; ++i) {
      final GraphMutation mutation = mutations[i];
      switch (mutation.type) {
        case GraphMutationType.setValue:
          // TODO(thatguy): Log an error if a Module writes to a value it's not
          // supposed to write to.
          final Set<String> keys = _instance.labelMapper
              .getValueLabels(appliedMutationsGraph.node(mutation.nodeId));
          if (!keys.contains(mutation.valueKey)) {
            _log.severe("Not authorized to write ${mutation.valueKey} on node "
                "${mutation.nodeId} with $keys; $_instance.");
            mutations.removeAt(i--);
          }
          break;
        case GraphMutationType.addEdge:
          // If the edge matches the display path expression, then set a unique
          // representation value on the target, so that parent module can find
          // the UI widget corresponding to it.
          final bool isDisplayNode = _instance.manifest.display.any(
              (final PathExpr d) => d.properties.last.labels
                  .map((final Label l) => l.uri.toString())
                  .toSet()
                  .containsAll(mutation.labels));
          if (isDisplayNode) {
            extra.add(new GraphMutation.setValue(mutation.targetNodeId,
                Binding.displayNodeLabel, BuiltinString.write('true')));
          }

          final Iterable<String> recipeLabels = _instance.labelMapper
                  .getRecipeLabels(
                      appliedMutationsGraph.node(mutation.originNodeId),
                      mutation.labels.toSet()) ??
              mutation.labels;

          mutations[i] = new GraphMutation(GraphMutationType.addEdge,
              edgeId: mutation.edgeId,
              originNodeId: mutation.originNodeId,
              targetNodeId: mutation.targetNodeId,
              labels: recipeLabels.toList());
          break;
        default:
          break;
      }
    }
    mutations.addAll(extra);
  }
}
