// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:typed_data';

import '../graph.dart';
import '../mutation.dart';

/// A convenience class for tests to avoid boilerplate when performing simple
/// mutations of Graph state.
class MutationHelper {
  final Graph graph;
  MutationHelper(this.graph);

  Node addNode({String id}) {
    NodeId nodeId;
    graph.mutate((GraphMutator mutator) {
      if (id != null) {
        nodeId = new NodeId.fromString(id);
        mutator.apply(new GraphMutation.addNode(nodeId));
      } else {
        nodeId = mutator.addNode().id;
      }
    });
    return graph.node(nodeId);
  }

  void removeNode(Node node) {
    graph.mutate((GraphMutator mutator) {
      mutator.removeNode(node.id);
    });
  }

  Edge addEdge(Node origin, List<String> labels, Node target,
      {String id}) {
    EdgeId edgeId;
    graph.mutate((GraphMutator mutator) {
      if (target == null) {
        target = mutator.addNode();
      }
      if (id == null) {
        edgeId = mutator.addEdge(origin.id, labels, target.id).id;
      } else {
        edgeId = new EdgeId.fromString(id);
        mutator.apply(
            new GraphMutation.addEdge(edgeId, origin.id, target.id, labels));
      }
    });
    return graph.edge(edgeId);
  }

  void removeEdge(Edge edge) {
    graph.mutate((GraphMutator mutator) {
      mutator.removeEdge(edge.id);
    });
  }

  void setValue(Node node, String key, List<int> value) {
    graph.mutate((GraphMutator mutator) {
      mutator.setValue(
          node.id, key, value != null ? new Uint8List.fromList(value) : null);
    });
  }
}
