// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:typed_data';

import 'package:modular_core/graph/graph.dart';
import 'package:modular_core/graph/lazy_cloned_graph.dart';
import 'package:modular_core/graph/mem_graph.dart';
import 'package:modular_core/graph/mutation.dart';

import 'package:test/test.dart';

void main() {
  Graph wrapped;
  Graph lazy;
  Node n0;
  Node n1;

  group('LazyClonedGraph:', () {
    setUp(() {
      wrapped = new MemGraph();
      wrapped.mutate((GraphMutator mutator) {
        // Create some nodes.
        n0 = mutator.addNode();
        n1 = mutator.addNode();
      });
      lazy = new LazyClonedGraph(wrapped);
      expect(lazy.state.containsNodeId(n0.id), isTrue);
      expect(lazy.state.containsNodeId(n1.id), isTrue);
    });

    test('Remove edge', () {
      // Add edge to wrapped graph and ensure that it is visible in both graphs.
      Edge e;
      wrapped.mutate((mutator) {
        e = mutator.addEdge(n0.id, [], n1.id);
      });
      expect(wrapped.state.containsEdgeId(e.id), isTrue);
      expect(lazy.state.containsEdgeId(e.id), isTrue);

      // Remove it from the lazy graph.
      lazy.mutate((mutator) {
        mutator.removeEdge(e.id);
      });
      expect(lazy.state.containsEdgeId(e.id), isFalse); // removed from lazy
      expect(wrapped.state.containsEdgeId(e.id), isTrue); // still in wrapped
    });

    test('Add edge', () {
      Edge e;
      lazy.mutate((mutator) {
        e = mutator.addEdge(n0.id, [], n1.id);
      });
      expect(lazy.state.containsEdgeId(e.id), isTrue);
      expect(wrapped.state.containsEdgeId(e.id), isFalse);
    });

    test('Remove node', () {
      lazy.mutate((mutator) {
        mutator.removeNode(n0.id);
      });
      expect(lazy.state.containsNodeId(n0.id), isFalse);
      expect(wrapped.state.containsNodeId(n0.id), isTrue);
    });

    test('Remove node with in/out edges', () {
      Edge e0, e1;
      wrapped.mutate((mutator) {
        e0 = mutator.addEdge(n0.id, [], n1.id);
        e1 = mutator.addEdge(n1.id, [], n0.id);
      });
      expect(lazy.node(n0.id).outEdges.first.id, equals(e0.id));
      expect(lazy.node(n0.id).inEdges.first.id, equals(e1.id));

      lazy.mutate((mutator) {
        mutator.removeNode(n1.id);
      });
      expect(lazy.node(n0.id).outEdges, isEmpty);
      expect(lazy.node(n0.id).inEdges, isEmpty);
      expect(wrapped.node(n0.id).outEdges.first, equals(e0));
      expect(wrapped.node(n0.id).inEdges.first, equals(e1));
    });

    test('Set node value', () {
      // Set a value, and verify that it shows up in the lazy graph.
      wrapped.mutate((mutator) {
        mutator.setValue(n0.id, 'key0', new Uint8List.fromList([1, 2, 3]));
      });
      expect(lazy.node(n0.id), isNotNull);
      expect(lazy.node(n0.id).getValue('key0'), equals(n0.getValue('key0')));

      // Overwrite the value in the lazy graph, and verify that it doesn't
      // affect the wrapped graph.
      lazy.mutate((mutator) {
        mutator.setValue(n0.id, 'key0', new Uint8List.fromList([1, 2]));
      });
      expect(lazy.node(n0.id).getValue('key0'), equals([1, 2]));
      expect(n0.getValue('key0'), equals([1, 2, 3]));

      // Clear the value in the lazy graph, and verify that it doesn't affect
      // the wrapped graph.
      lazy.mutate((mutator) {
        mutator.setValue(n0.id, 'key0', null);
      });
      expect(lazy.node(n0.id).getValue('key0'), isNull);
      expect(n0.getValue('key0'), equals([1, 2, 3]));
    });

    test('Set node value 2', () {
      // Set a value, and verify that it shows up in the lazy graph.
      wrapped.mutate((mutator) {
        mutator.setValue(n0.id, 'key0', new Uint8List.fromList([1, 2, 3]));
      });
      expect(lazy.node(n0.id), isNotNull);
      expect(lazy.node(n0.id).getValue('key0'), equals(n0.getValue('key0')));

      // Set a new value in the lazy graph, and verify that both values are in
      // the lazy graph while not affecting the wrapped graph.
      lazy.mutate((mutator) {
        mutator.setValue(n0.id, 'key1', new Uint8List.fromList([1, 2]));
      });
      expect(lazy.node(n0.id).getValue('key0'), equals(n0.getValue('key0')));
      expect(lazy.node(n0.id).getValue('key1'), equals([1, 2]));
      expect(wrapped.node(n0.id).getValue('key1'), isNull);

      // Overwrite the value in the wrapped graph. The value of key0 should come
      // from the wrapped graph, i.e. it should propagate to the lazy graph as
      // it was never modified in the lazy graph.
      wrapped.mutate((mutator) {
        mutator.setValue(n0.id, 'key0', new Uint8List.fromList([1, 2, 3]));
      });
      expect(lazy.node(n0.id).getValue('key0'), equals(n0.getValue('key0')));
    });

    test('Observation (node)', () {
      int observerCount = 0;
      List<GraphMutation> lastMutations;
      lazy.addObserver((final GraphEvent event) {
        observerCount++;
        lastMutations = event.mutations.toList();
      });

      Node n2;
      wrapped.mutate((mutator) {
        mutator.setValue(n0.id, 'key0', new Uint8List.fromList([1, 2, 3]));
        n2 = mutator.addNode();
      });
      expect(observerCount, equals(1));
      expect(lastMutations.length, equals(2));
      expect(lastMutations[0].type, equals(GraphMutationType.setValue));
      expect(lastMutations[1].type, equals(GraphMutationType.addNode));

      // Remove n0 from lazy.
      lazy.mutate((mutator) {
        mutator.removeNode(n0.id);
      });
      expect(observerCount, equals(2));
      expect(lastMutations.length, equals(1));
      expect(lastMutations[0].type, equals(GraphMutationType.removeNode));

      // Mutations from the wrapped graph on n0 should get masked.
      observerCount = 0;
      wrapped.mutate((mutator) {
        mutator.setValue(n0.id, 'key0', new Uint8List.fromList([4, 5]));
        mutator.removeNode(n0.id);
        mutator.apply(new GraphMutation.addNode(n0.id));
      });
      expect(observerCount, equals(0));

      wrapped.mutate((mutator) {
        mutator.setValue(n0.id, 'key0', new Uint8List.fromList([4, 5]));
        mutator.setValue(n2.id, 'key0', new Uint8List.fromList([5]));
        mutator.removeNode(n0.id);
        mutator.apply(new GraphMutation.addNode(n0.id));
      });
      expect(observerCount, equals(1));
      expect(lastMutations.length, equals(1));
      expect(lastMutations[0].type, equals(GraphMutationType.setValue));
      expect(lastMutations[0].nodeId, equals(n2.id));
    });

    test('Observer (node value)', () {
      int observerCount = 0;
      List<GraphMutation> lastMutations;
      lazy.addObserver((final GraphEvent event) {
        observerCount++;
        lastMutations = event.mutations.toList();
      });

      lazy.mutate((mutator) {
        mutator.removeNode(n0.id);
        mutator.setValue(n1.id, 'key0', new Uint8List.fromList([0]));
      });
      expect(observerCount, equals(1));
      expect(lastMutations.length, equals(2));

      // Value modifications in the wrapped graph on n0 should not propagate.
      // Value modifications on n1 with a value key that has been overwritten in
      // the lazy graph should not propagate.
      wrapped.mutate((mutator) {
        mutator.setValue(n0.id, 'key1', new Uint8List.fromList([1, 2]));
        mutator.setValue(n1.id, 'key1', new Uint8List.fromList([1, 2]));
        mutator.setValue(n1.id, 'key0', new Uint8List.fromList([1, 2]));
      });
      expect(observerCount, equals(2));
      expect(lastMutations.length, equals(1));
      expect(lastMutations[0].type, equals(GraphMutationType.setValue));
      expect(lastMutations[0].nodeId, equals(n1.id));
      expect(lastMutations[0].valueKey, equals('key1'));
    });

    test('Observation (edge)', () {
      int observerCount = 0;
      List<GraphMutation> lastMutations;
      lazy.addObserver((final GraphEvent event) {
        observerCount++;
        lastMutations = event.mutations.toList();
      });

      // The mutations below shouldn't be reported when coalesced.
      wrapped.mutate((mutator) {
        Edge e = mutator.addEdge(n0.id, [], n1.id);
        mutator.removeEdge(e.id);
      });
      expect(observerCount, equals(0));

      Edge e0, e1, e2;
      wrapped.mutate((mutator) {
        e0 = mutator.addEdge(n0.id, [], n1.id);
      });
      expect(observerCount, equals(1));
      expect(lastMutations.length, equals(1));
      expect(lastMutations[0].type, equals(GraphMutationType.addEdge));

      Node n2;
      wrapped.mutate((mutator) {
        mutator.removeEdge(e0.id);
        n2 = mutator.addNode();
        e0 = mutator.addEdge(n1.id, [], n2.id);
      });
      expect(observerCount, equals(2));
      expect(lastMutations.length, equals(3));
      expect(lastMutations[0].type, equals(GraphMutationType.removeEdge));
      expect(lastMutations[1].type, equals(GraphMutationType.addNode));
      expect(lastMutations[2].type, equals(GraphMutationType.addEdge));

      // Remove n0 in the lazy graph. Edges added to the wrapped graph that are
      // connected to n0 should not be reported.
      lazy.mutate((mutator) {
        mutator.removeNode(n0.id);
      });
      expect(observerCount, equals(3));

      observerCount = 0;
      wrapped.mutate((mutator) {
        e1 = mutator.addEdge(n0.id, [], n1.id);
        e2 = mutator.addEdge(n1.id, [], n0.id);
      });
      expect(observerCount, equals(0));

      // Removing such edges should also not get reported.
      wrapped.mutate((mutator) {
        mutator.removeEdge(e1.id);
        mutator.removeEdge(e2.id);
      });
      expect(observerCount, equals(0));

      // e0 is connected to n1 and n2, both of which exist in the lazy graph.
      // Once e0 is removed from the lazy graph, any mutations from the wrapped
      // graph with its ID should not be reported.
      lazy.mutate((mutator) {
        mutator.removeEdge(e0.id);
      });
      expect(observerCount, equals(1));
      expect(lastMutations[0].type, equals(GraphMutationType.removeEdge));

      observerCount = 0;
      wrapped.mutate((mutator) {
        mutator.removeEdge(e0.id);
      });
      expect(observerCount, equals(0));

      wrapped.mutate((mutator) {
        mutator.apply(new GraphMutation.addEdge(e0.id, n1.id, n2.id, []));
      });
      expect(observerCount, equals(0));
    });
  });
}
