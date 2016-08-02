// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:modular_core/graph/graph.dart';

import '../../bindings.dart';

/// Create a [GraphEvent] whose mutations might have resulted in the current
/// state of the specified [Graph].
GraphEvent createGraphEventWithPlausibleHistoryOf(Graph graph) {
  assert(graph != null);
  final List<GraphMutation> mutations = <GraphMutation>[];

  for (Node node in graph.nodes) {
    mutations.add(new GraphMutation.addNode(node.id));
    for (String key in node.valueKeys) {
      mutations.add(new GraphMutation(GraphMutationType.setValue,
          nodeId: node.id, valueKey: key, newValue: node.getValue(key)));
    }
  }
  for (Edge edge in graph.edges) {
    mutations.add(new GraphMutation(GraphMutationType.addEdge,
        edgeId: edge.id,
        originNodeId: edge.origin.id,
        targetNodeId: edge.target.id,
        labels: edge.labels));
  }
  return new GraphEvent(graph, mutations);
}

// Returns true for display nodes.
bool isDisplayNode(final Node target) =>
    target.getValue(Binding.displayNodeLabel) != null;
