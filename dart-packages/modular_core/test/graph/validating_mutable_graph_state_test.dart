// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:modular_core/graph/graph.dart';
import 'package:modular_core/graph/mem_graph.dart';
import 'package:modular_core/graph/mutation.dart';
import 'package:modular_core/graph/validating_mutable_graph_state.dart';

import 'package:test/test.dart';

void main() {
  MemGraph graph;
  ValidatingMutableGraphState state;
  Node n1, n2;
  Edge e1;

  EdgeId newEdgeId;
  NodeId newNodeId;
  NodeId missingNodeId;

  setUp(() {
    graph = new MemGraph();
    graph.mutate((GraphMutator m) {
      n1 = m.addNode();
      n2 = m.addNode();
      e1 = m.addEdge(n1.id, ['foo'], n2.id);
    });
    state = new ValidatingMutableGraphState(graph.state);

    newEdgeId = new EdgeId.fromString('new_edge');
    newNodeId = new NodeId.fromString('new_node');
    missingNodeId = new NodeId.fromString('missing_node');
  });

  test('Missing Origin and Target Node', () {
    bool caughtMissingOrigin = false;
    try {
      state.applyMutation(
          new GraphMutation.addEdge(newEdgeId, missingNodeId, n1.id, ['bar']));
    } on MissingOriginNode {
      caughtMissingOrigin = true;
    }
    expect(caughtMissingOrigin, isTrue);

    bool caughtMissingTarget = false;
    try {
      state.applyMutation(
          new GraphMutation.addEdge(newEdgeId, n1.id, missingNodeId, ['bar']));
    } on MissingTargetNode {
      caughtMissingTarget = true;
    }
    expect(caughtMissingTarget, isTrue);
  });

  test('Remove existing Edge and Node', () {
    final GraphMutation removeEdge =
        new GraphMutation.removeEdge(e1.id, n1.id, n2.id, e1.labels);
    final GraphMutation removeNode = new GraphMutation.removeNode(n1.id);
    expect(state.applyMutation(removeEdge), isTrue);
    expect(state.applyMutation(removeNode), isTrue);
  });

  // Removing a node or edge that doesn't exist in the graph is OK.  In this
  // case, applyMutation() returns false.
  test('Remove missing Edge and Node', () {
    final GraphMutation removeEdge =
        new GraphMutation.removeEdge(newEdgeId, n1.id, n2.id, e1.labels);
    final GraphMutation removeNode =
        new GraphMutation.removeNode(missingNodeId);
    expect(state.applyMutation(removeEdge), isFalse);
    expect(state.applyMutation(removeNode), isFalse);
  });

  // Adding an node or edge that already exists is OK, as long as they are
  // compatible.  In this case, applyMutation() returns false.
  test('Add compatible Node and Edge', () {
    final GraphMutation addNode = new GraphMutation.addNode(n1.id);
    expect(state.applyMutation(addNode), isFalse);
    final GraphMutation addEdge =
        new GraphMutation.addEdge(e1.id, n1.id, n2.id, ['foo']);
    expect(state.applyMutation(addEdge), isFalse);
  });

  // Adding an edge that already exists results in a thrown exception when the
  // mutation conflicts with the existing state, e.g. when the origin-node
  // specified in the mutation differs from the origin-node of the existing
  // edge.
  test('Add conflicting Edge', () {
    bool caughtConflictingOriginNode = false;
    try {
      state.applyMutation(
          new GraphMutation.addEdge(e1.id, n2.id, n2.id, e1.labels));
    } on ConflictingOriginNode {
      caughtConflictingOriginNode = true;
    }
    expect(caughtConflictingOriginNode, isTrue);

    bool caughtConflictingTargetNode = false;
    try {
      state.applyMutation(
          new GraphMutation.addEdge(e1.id, n1.id, n1.id, e1.labels));
    } on ConflictingTargetNode {
      caughtConflictingTargetNode = true;
    }
    expect(caughtConflictingTargetNode, isTrue);

    bool caughtConflictingEdgeLabels = false;
    try {
      state.applyMutation(
          new GraphMutation.addEdge(e1.id, n1.id, n2.id, ['conflicting']));
    } on ConflictingEdgeLabels {
      caughtConflictingEdgeLabels = true;
    }
    expect(caughtConflictingEdgeLabels, isTrue);
  });

  test('Add new Node and Edge', () {
    expect(state.containsNodeId(newNodeId), isFalse);
    final GraphMutation addNode = new GraphMutation.addNode(newNodeId);
    expect(state.applyMutation(addNode), isTrue);
    expect(state.containsNodeId(newNodeId), isTrue);

    expect(state.containsEdgeId(newEdgeId), isFalse);
    final GraphMutation addEdge =
        new GraphMutation.addEdge(newEdgeId, n2.id, newNodeId, ['foo']);
    expect(state.applyMutation(addEdge), isTrue);
    expect(state.containsEdgeId(newEdgeId), isTrue);
  });
}
