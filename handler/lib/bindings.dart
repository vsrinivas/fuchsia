// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:modular_core/entity/schema.dart';
import 'package:modular_core/entity/entity.dart';
import 'package:modular_core/graph/graph.dart';
import 'package:modular_core/graph/buffering_mutator.dart';
import 'package:handler/graph/impl/graph_util.dart';
import 'package:collection/collection.dart';

/// Binding manages the metadata part of the graph defining the list of inputs
/// and outputs of a module instance. A binding has the following structure once
/// stored in the graph:
///
/// internal:instance
///   -> internal:verb <builtin:string>
///   -> internal:input* <builtin:string>
///   -> internal:output* <builtin:string>
///
/// The representation values of input and output nodes are the IDs of the edges
/// matched by the input/output path expressions of the module manifest. The
/// input edges also contain edges matched by the scope expressions in the
/// recipe step.
///
/// The representation value of the verb node is the semantic label.

const String _verbProperty = 'verb';
const String _inputProperty = 'input';
const String _outputProperty = 'output';
const String _bindingType = 'internal:moduleBinding';
final Schema _bindingSchema = new Schema(
    _bindingType,
    [
      new Property.string(_verbProperty),
      new Property.string(_inputProperty, isRepeated: true),
      new Property.string(_outputProperty, isRepeated: true)
    ],
    autoPublish: true);

class Binding {
  static const String _instanceLabel = 'internal:instance';
  static const String displayNodeLabel = 'internal:display';
  static const String mutationTag = 'Bindings';

  /// The graph the module instance information is stored in. Usually the
  /// session graph for the session the module instance runs in.
  final Graph _graph;

  /// Entity that encodes the data for one Module instance.
  Entity _binding;

  /// Each module instance has a metadata node associated with it which is
  /// located under the metadataRoot node. The Binding constructor finds it, or
  /// creates it if it does not exist.
  Binding(this._graph, final Node metadataRoot, final String manifestVerb,
      final Set<Edge> inputEdges) {
    final Set<String> inputEdgeIds = _edgeIdStrings(inputEdges);

    final Iterable<Edge> instanceEdges =
        metadataRoot.outEdgesWithLabels([_instanceLabel]);

    for (final Edge instanceEdge in instanceEdges) {
      final Entity instanceEntity = new Entity.fromNode(instanceEdge.target);

      if (instanceEntity[_verbProperty] != manifestVerb) {
        continue;
      }

      if (!const SetEquality<String>()
          .equals(instanceEntity[_inputProperty].toSet(), inputEdgeIds)) {
        continue;
      }

      // We found the metadata node that stores the bindings for the module
      // instance with the given verb and set of inputs.
      _binding = instanceEntity;
      return;
    }

    // There is no matching instance metadata node; let's create one. The input
    // edges are not created here but in setInput() below.
    _binding = new Entity([_bindingSchema.type]);
    _binding[_verbProperty] = manifestVerb;
  }

  /// Updates the input edge list of this binding.
  void setInput(final Iterable<Edge> inputEdges) {
    final Set<String> inputEdgeIds = _edgeIdStrings(inputEdges);
    _binding[_inputProperty] = inputEdgeIds.toList();

    _graph.mutate((final GraphMutator mutator) {
      _binding.save(mutator);
    }, tag: mutationTag);
  }

  /// Returns the list of output edges attached to this module instance.
  Iterable<Edge> loadOutputEdges() {
    _binding.reload();

    return _binding[_outputProperty]
        .map((final String edgeIdString) =>
            _graph.edge(new EdgeId.fromString(edgeIdString)))
        .where((final Edge e) => e != null);
  }

  /// Computes the graph mutations needed to update the bindings corresponding
  /// to the module outputs represented by the given mutation.
  Iterable<GraphMutation> updateOutputMutations(final GraphMutation mutation) {
    _binding.reload();

    final BufferingMutator mutator = new BufferingMutator(_graph);

    final EdgeId edgeId = mutation.edgeId;
    if (mutation.type == GraphMutationType.addEdge &&
        !_binding[_outputProperty].contains(edgeId.toString())) {
      _binding[_outputProperty].add(edgeId.toString());
    } else if (mutation.type == GraphMutationType.removeEdge) {
      _binding[_outputProperty].remove(edgeId.toString());
    }

    _binding.save(mutator);
    return mutator.mutations;
  }

  /// Converts a set of edges to the set of their IDs as Strings. Filters out
  /// edges that should not be recorded in bindings.
  static Set<String> _edgeIdStrings(final Iterable<Edge> edges) =>
      new Set<String>.from(edges
          .where((final Edge edge) => _shouldSyncEdge(edge))
          .map((final Edge edge) => edge.id.toString()));

  /// Returns whether the edge should be stored into the bindings. Currently,
  /// edges with display nodes as targets are excluded.
  static bool _shouldSyncEdge(final Edge edge) => !isDisplayNode(edge.target);
}
