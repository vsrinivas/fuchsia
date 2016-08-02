// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:typed_data';

import 'package:test/test.dart';

import 'package:modular_core/graph/graph.dart';
import 'package:modular_core/graph/id.dart';
import 'package:modular_core/graph/mixins/buffered_mutate_mixin.dart';
import 'package:modular_core/graph/test_utils/fake_graph.dart';

class TestMixinGraph extends BufferedMutateMixin {
  FakeGraph baseGraph;

  TestMixinGraph() {
    baseGraph = new FakeGraph(
        nodeIdGenerator: _nodeIdGenerator, edgeIdGenerator: _edgeIdGenerator);
  }

  int nodeCnt = 1;
  NodeId _nodeIdGenerator() {
    return new NodeId.fromString('n${nodeCnt++}');
  }

  int edgeCnt = 1;
  EdgeId _edgeIdGenerator() {
    return new EdgeId.fromString('e${edgeCnt++}');
  }

  List<GraphMutation> appliedMutations;
  dynamic tag;

  @override
  Graph get mixinGraph => baseGraph;

  @override
  @override
  void applyMutations(List<GraphMutation> mutations, {dynamic tag}) {
    assert(appliedMutations == null);
    appliedMutations = mutations;
    this.tag = tag;
  }

  void reset() {
    appliedMutations = null;
    tag = null;
  }
}

void main() {
  TestMixinGraph graph;
  setUp(() {
    graph = new TestMixinGraph();
  });
  test('mutations are applied', () {
    // Show that when we call .mutate() on the graph, we get mutations
    // in applyMutations().
    graph.mutate((GraphMutator mutator) {
      mutator.addNode();
    });

    expect(graph.appliedMutations,
        equals([new GraphMutation.addNode(new NodeId.fromString('n1'))]));

    graph.reset();
    // However if we don't do any mutation, we shouldn't see an applyMutations()
    // call.
    graph.mutate((GraphMutator mutator) {});
    expect(graph.appliedMutations, isNull);
  });
  test('tag forwarded', () {
    graph.mutate((GraphMutator mutator) {
      mutator.addNode(); // give it something that matters
    }, tag: 'tag!');

    expect(graph.tag, equals('tag!'));
  });
  test('base graph', () {
    // The mixinGraph getter is used as the read-only state that is referenced
    // when doing mutations. Show that it's used by removing a node that exists
    // in the grpah state.
    graph.baseGraph.fakeNodes = [new FakeNode('foo')];
    graph.mutate((GraphMutator mutator) {
      mutator.removeNode(new NodeId.fromString('foo'));
    });
    // We just want to show that no exception is thrown.
  });
  test('ID generator', () {
    // Show that the IDs we create through the edgeIdGenerator and
    // nodeIdGenerator accessors are used.
    graph.mutate((GraphMutator mutator) {
      Node n1 = mutator.addNode();
      Node n2 = mutator.addNode();
      mutator.addEdge(n1.id, ['label'], n2.id);
    });

    expect(
        graph.appliedMutations,
        equals([
          new GraphMutation.addNode(new NodeId.fromString('n1')),
          new GraphMutation.addNode(new NodeId.fromString('n2')),
          new GraphMutation.addEdge(
              new EdgeId.fromString('e1'),
              new NodeId.fromString('n1'),
              new NodeId.fromString('n2'),
              ['label'])
        ]));
  });

  test('References', () {
    // We expect the Node and Edge objects returned by the GraphMutator
    // to be bound to a view of the data as it would look after mutations are
    // applied.
    Node n1, n2;
    Edge e1;
    graph.mutate((GraphMutator mutator) {
      n1 = mutator.addNode();
      n2 = mutator.addNode();
      e1 = mutator.addEdge(n1.id, ['label'], n2.id);

      expect(e1.origin, equals(n1));
      expect(e1.target, equals(n2));
      expect(e1.labels, equals(['label']));

      mutator.setValue(n1.id, 'key', new Uint8List.fromList([0]));
      expect(n1.getValue('key'), isNotNull);
    });

    // Now that mutate() is done, the refs should be re-assigned to the base
    // Graph [baseGraph]. They are invalid because FakeGraph doesn't actually
    // apply any mutations.
    expect(n1.isDeleted, isTrue);

    // However if we fake apply them ourselves, we should see the refs become
    // valid and show real data.
    graph.baseGraph.fakeNodes = [
      new FakeNode(n1.id.toString())
        ..values = {
          'key': new Uint8List.fromList([0])
        }
    ];
    graph.baseGraph.fakeEdges = [
      new FakeEdge(
          e1.id.toString(), n1.id.toString(), n2.id.toString(), ['label'])
    ];
    expect(n1.isDeleted, isFalse);
    expect(e1.origin, equals(n1));
    expect(e1.target, equals(n2));
    expect(e1.labels, equals(['label']));
    expect(n1.getValue('key'), isNotNull);
  });
}
