// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:typed_data';

import 'package:test/test.dart';

import 'package:modular_core/graph/graph.dart';
import 'package:modular_core/graph/buffering_mutator.dart';
import 'package:modular_core/graph/test_utils/fake_graph.dart';

void main() {
  FakeGraph graph;
  BufferingMutator m;
  setUp(() {
    int nodeIdCnt = 1;
    int edgeIdCnt = 1;
    graph = new FakeGraph(
        nodeIdGenerator: () => new NodeId.fromString('n${nodeIdCnt++}'),
        edgeIdGenerator: () => new EdgeId.fromString('e${edgeIdCnt++}'));
    m = new BufferingMutator(graph);
  });

  test('addNode', () {
    Node ref1 = m.addNode(); // 1
    expect(ref1.id, equals(new NodeId.fromString('n1')));
    expect(m.mutations,
        equals([new GraphMutation.addNode(new NodeId.fromString('n1'))]));

    Node ref2 = m.addNode(); // 2
    expect(ref2.id, equals(new NodeId.fromString('n2')));
    expect(
        m.mutations,
        equals([
          new GraphMutation.addNode(new NodeId.fromString('n1')),
          new GraphMutation.addNode(new NodeId.fromString('n2'))
        ]));
  });

  group('removeNode:', () {
    test('state in graph', () {
      // Removing a node that doesn't exist is an error.
      final NodeId id = new NodeId.fromString('foo');
      expect(() => m.removeNode(id),
          throwsA(new isInstanceOf<FailedGraphMutation>()));

      // But if this node exists already, it should work fine.
      graph.fakeNodes = [new FakeNode('foo')];
      m.removeNode(id);
      expect(m.mutations, equals([new GraphMutation.removeNode(id)]));
    });

    test('state in mutations', () {
      Node node = m.addNode();
      m.removeNode(node.id);
      expect(
          m.mutations,
          equals([
            new GraphMutation.addNode(node.id),
            new GraphMutation.removeNode(node.id)
          ]));

      // Trying to remove it again is also a failure.
      expect(() => m.removeNode(node.id),
          throwsA(new isInstanceOf<FailedGraphMutation>()));
    });

    test('in-edges and out-edges: state in graph', () {
      graph.fakeNodes = [
        new FakeNode('A'),
        new FakeNode('B'),
        new FakeNode('C')
      ];
      graph.fakeEdges = [
        new FakeEdge('AB', 'A', 'B', []),
        new FakeEdge('BC', 'B', 'C', []),
        new FakeEdge('BC2', 'B', 'C', [])
      ];
      // Let's remove edgeBC2 ourselves, and see that it isn't removed again.
      m.removeEdge(new EdgeId.fromString('BC2'));
      // Removing nodeB should also remove edgeAB and edgeBC.
      m.removeNode(new NodeId.fromString('B'));
      expect(
          m.mutations,
          equals([
            new GraphMutation.removeEdge(new EdgeId.fromString('BC2'),
                new NodeId.fromString('B'), new NodeId.fromString('C'), []),
            new GraphMutation.removeEdge(new EdgeId.fromString('AB'),
                new NodeId.fromString('A'), new NodeId.fromString('B'), []),
            new GraphMutation.removeEdge(new EdgeId.fromString('BC'),
                new NodeId.fromString('B'), new NodeId.fromString('C'), []),
            new GraphMutation.removeNode(new NodeId.fromString('B'))
          ]));
    });

    test('in-edges and out-edges: state in mutations', () {
      Node nodeA = m.addNode();
      Node nodeB = m.addNode();
      Node nodeC = m.addNode();

      Edge edgeAB = m.addEdge(nodeA.id, [], nodeB.id);
      Edge edgeBC = m.addEdge(nodeB.id, [], nodeC.id);
      Edge edgeBC2 = m.addEdge(nodeB.id, [], nodeC.id);
      m.removeEdge(edgeBC2.id);

      // Removing nodeB should also remove edgeAB and edgeBC.
      m.removeNode(nodeB.id);
      expect(
          m.mutations,
          equals([
            new GraphMutation.addNode(nodeA.id),
            new GraphMutation.addNode(nodeB.id),
            new GraphMutation.addNode(nodeC.id),
            new GraphMutation.addEdge(edgeAB.id, nodeA.id, nodeB.id, []),
            new GraphMutation.addEdge(edgeBC.id, nodeB.id, nodeC.id, []),
            new GraphMutation.addEdge(edgeBC2.id, nodeB.id, nodeC.id, []),
            new GraphMutation.removeEdge(edgeBC2.id, nodeB.id, nodeC.id, []),
            new GraphMutation.removeEdge(edgeAB.id, nodeA.id, nodeB.id, []),
            new GraphMutation.removeEdge(edgeBC.id, nodeB.id, nodeC.id, []),
            new GraphMutation.removeNode(nodeB.id)
          ]));
    });
  });

  group('setValue:', () {
    // BufferedMutator will attempt to find the previous value from previous
    // mutations, and fall back to the read-only graph state.
    test('node does not exist', () {
      // The node 'foo' doesn't exist.
      expect(
          () => m.setValue(
              new NodeId.fromString('foo'), 'key', new Uint8List.fromList([0])),
          throwsA(new isInstanceOf<FailedGraphMutation>()));
    });
    test('no previous value', () {
      final NodeId id = new NodeId.fromString('foo');
      graph.fakeNodes = [new FakeNode(id.toString())];
      m.setValue(id, 'key', new Uint8List.fromList([0]));

      expect(
          m.mutations,
          equals([
            new GraphMutation.setValue(new NodeId.fromString('foo'), 'key',
                new Uint8List.fromList([0]))
          ]));
    });
    test('previous state value', () {
      graph.fakeNodes = [
        new FakeNode('foo')
          ..values = {
            'key': [0]
          }
      ];

      m.setValue(
          new NodeId.fromString('foo'), 'key', new Uint8List.fromList([1]));

      expect(
          m.mutations,
          equals([
            new GraphMutation.setValue(new NodeId.fromString('foo'), 'key',
                new Uint8List.fromList([1]))
          ]));
    });
    test('previous mutation value', () {
      graph.fakeNodes = [
        new FakeNode('foo')
          ..values = {
            'key': [0]
          }
      ];

      m.setValue(
          new NodeId.fromString('foo'), 'key', new Uint8List.fromList([1]));
      // This time around we should see [1] as the value, not [0].
      m.setValue(
          new NodeId.fromString('foo'), 'key', new Uint8List.fromList([2]));

      expect(
          m.mutations,
          equals([
            new GraphMutation.setValue(new NodeId.fromString('foo'), 'key',
                new Uint8List.fromList([1])),
            new GraphMutation.setValue(new NodeId.fromString('foo'), 'key',
                new Uint8List.fromList([2]))
          ]));
    });
    test('no change', () {
      // When the value hasn't change we shouldn't see a mutation.
      graph.fakeNodes = [
        new FakeNode('foo')
          ..values = {
            'key': [0]
          }
      ];

      m.setValue(
          new NodeId.fromString('foo'), 'key', new Uint8List.fromList([0]));

      expect(m.mutations, equals([]));
    });
  });

  group('addEdge:', () {
    test('validation errors', () {
      // Both the origin and target nodes must exist before adding an edge.
      NodeId origin = new NodeId.fromString('origin');
      NodeId target = new NodeId.fromString('target');

      expect(() => m.addEdge(origin, ['label'], target),
          throwsA(new isInstanceOf<FailedGraphMutation>()));

      graph.fakeNodes = [new FakeNode('origin')];
      expect(() => m.addEdge(origin, ['label'], target),
          throwsA(new isInstanceOf<FailedGraphMutation>()));

      graph.fakeNodes = [new FakeNode('target')];
      expect(() => m.addEdge(origin, ['label'], target),
          throwsA(new isInstanceOf<FailedGraphMutation>()));
    });
    test('target specified', () {
      Node n1 = m.addNode();
      Node n2 = m.addNode();
      m.addEdge(n1.id, ['label'], n2.id);

      expect(
          m.mutations,
          equals([
            new GraphMutation.addNode(n1.id),
            new GraphMutation.addNode(n2.id),
            new GraphMutation.addEdge(
                new EdgeId.fromString('e1'), n1.id, n2.id, ['label'])
          ]));
    });
    test('target not specified', () {
      graph.fakeNodes = [new FakeNode('origin')];
      m.addEdge(new NodeId.fromString('origin'), ['label']);

      expect(
          m.mutations,
          equals([
            new GraphMutation.addNode(new NodeId.fromString('n1')),
            new GraphMutation.addEdge(
                new EdgeId.fromString('e1'),
                new NodeId.fromString('origin'),
                new NodeId.fromString('n1'),
                ['label'])
          ]));
    });
  });

  group('removeEdge:', () {
    // Like setValue(), removeEdge() attempts to find previous state in the
    // mutation list, and fall back to the graph state.
    test('no previous state', () {
      // Trying to remove an edge that doesn't exist throws an error.
      expect(() => m.removeEdge(new EdgeId.fromString('edgeId')),
          throwsA(new isInstanceOf<FailedGraphMutation>()));
    });
    test('state in the graph', () {
      graph.fakeEdges = [
        new FakeEdge('edgeId', 'origin', 'target', ['label'])
      ];

      m.removeEdge(new EdgeId.fromString('edgeId'));

      expect(
          m.mutations,
          equals([
            new GraphMutation.removeEdge(
                new EdgeId.fromString('edgeId'),
                new NodeId.fromString('origin'),
                new NodeId.fromString('target'),
                ['label'])
          ]));

      // Trying to remove the edge again should error.
      expect(() => m.removeEdge(new EdgeId.fromString('edgeId')),
          throwsA(new isInstanceOf<FailedGraphMutation>()));
    });
    test('state in mutations', () {
      graph.fakeNodes = [new FakeNode('origin'), new FakeNode('target')];
      m.addEdge(new NodeId.fromString('origin'), ['label'],
          new NodeId.fromString('target'));

      m.removeEdge(new EdgeId.fromString('e1'));

      expect(
          m.mutations,
          equals([
            new GraphMutation.addEdge(
                new EdgeId.fromString('e1'),
                new NodeId.fromString('origin'),
                new NodeId.fromString('target'),
                ['label']),
            new GraphMutation.removeEdge(
                new EdgeId.fromString('e1'),
                new NodeId.fromString('origin'),
                new NodeId.fromString('target'),
                ['label'])
          ]));

      // Trying to remove the edge again should error.
      expect(() => m.removeEdge(new EdgeId.fromString('e1')),
          throwsA(new isInstanceOf<FailedGraphMutation>()));
    });
  });
}
