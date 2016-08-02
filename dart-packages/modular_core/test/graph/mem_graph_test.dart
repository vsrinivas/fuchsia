// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:typed_data';

import 'package:modular_core/graph/graph.dart';
import 'package:modular_core/graph/mem_graph.dart';
import 'package:modular_core/graph/test_utils/test_observer.dart';
import 'package:modular_core/graph/test_utils/mutation_helper.dart';

import 'package:test/test.dart';

void main() {
  final List<Uint8List> values = [
    new Uint8List.fromList([1, 2]),
    new Uint8List.fromList([2, 3]),
    new Uint8List.fromList([3, 4]),
  ];

  MemGraph g;
  MutationHelper helper;
  TestObserver o;

  setUp(() {
    g = new MemGraph(prefix: 'test');
    helper = new MutationHelper(g);
    o = new TestObserver();
    g.addObserver(o);
  });

  test('Node Creation', () {
    Node n1 = helper.addNode();
    Node n2 = helper.addNode();

    expect(g.nodes.length, equals(2));
    expect(g.edges.length, equals(0));
    expect(n1.isDeleted, isFalse);
    expect(n2.isDeleted, isFalse);
  });

  test('Node Deletion', () {
    Node n1 = helper.addNode();
    Node n2 = helper.addNode();
    Node n3 = helper.addNode();
    /* Edge e1 = */ helper.addEdge(n1, ["l"], n2);

    expect(g.nodes, equals([n1, n2, n3]));
    expect(g.edges.length, equals(1));

    helper.removeNode(n3);
    expect(g.nodes, equals([n1, n2]));
    expect(g.edges.length, equals(1));

    helper.removeNode(n2);
    expect(g.nodes, equals([n1]));
    // e1 is also deleted
    expect(g.edges.length, equals(0));
  });

  test('Node Values', () {
    Node n1 = helper.addNode();
    Node n2 = helper.addNode();

    helper.setValue(n1, 'a', values[0]);

    expect(n1.valueKeys, equals(['a']));
    expect(n2.valueKeys, equals([]));

    expect(n1.getValue('a'), equals(values[0]));
    expect(n1.getValue('b'), isNull);
    expect(n2.getValue('a'), isNull);

    o.clear();
    // Removing an non-existent value results in no changes and no
    // notification.
    helper.setValue(n1, 'b', null);
    expect(o.calls, equals(0));

    // Removing an existing value works.
    helper.setValue(n1, 'a', null);
    expect(n1.getValue('a'), isNull);
    expect(n1.valueKeys, equals([]));
    expect(o.calls, equals(1));
  });

  test('Edge Creation', () {
    Node n1 = helper.addNode();
    Node n2 = helper.addNode();
    Node n3 = helper.addNode();

    Edge e1 = helper.addEdge(n1, ['a', 'b'], n2);
    Edge e2 = helper.addEdge(n1, ['c'], n3);

    expect(g.edges, equals([e1, e2]));
    expect(e1 == e2, equals(false));
    expect(e1.id == e2.id, equals(false));

    expect(g.edge(e1.id), equals(e1));
    expect(g.edge(e2.id), equals(e2));

    expect(n1.outEdges, equals([e1, e2]));
    expect(n2.inEdges, equals([e1]));
    expect(n3.inEdges, equals([e2]));
  });

  test('Edge Deletion', () {
    Node n1 = helper.addNode();
    Node n2 = helper.addNode();
    Edge e1 = helper.addEdge(n1, ["l"], n2);

    expect(g.nodes, equals([n1, n2]));
    expect(g.edges.length, equals(1));

    helper.removeEdge(e1);
    expect(g.nodes, equals([n1, n2]));
    // e1 is deleted
    expect(g.edges.length, equals(0));
    expect(n1.outEdges, equals([]));
    expect(n2.inEdges, equals([]));
  });

  test('Notifications', () {
    Node n1, n2, n3;
    Edge e1, e2;
    g.mutate((GraphMutator mutator) {
      n1 = mutator.addNode();
      n2 = mutator.addNode();
      n3 = mutator.addNode();
      e1 = mutator.addEdge(n1.id, ['label'], n2.id);
      e2 = mutator.addEdge(n2.id, ['label'], n1.id);

      mutator.setValue(n1.id, 'key', new Uint8List.fromList([0]));

      mutator.removeNode(n3.id);
      mutator.removeEdge(e1.id);
    });

    expect(o.calls, equals(1));
    expect(
        o.last.mutations,
        equals([
          new GraphMutation.addNode(n1.id),
          new GraphMutation.addNode(n2.id),
          new GraphMutation.addNode(n3.id),
          new GraphMutation.addEdge(e1.id, n1.id, n2.id, ['label']),
          new GraphMutation.addEdge(e2.id, n2.id, n1.id, ['label']),
          new GraphMutation.setValue(n1.id, 'key', new Uint8List.fromList([0])),
          new GraphMutation.removeNode(n3.id),
          new GraphMutation.removeEdge(e1.id, n1.id, n2.id, ['label'])
        ]));

    // Show that notifiationTags are correctly added.
    o.clear();
    g.mutate((GraphMutator mutator) {
      mutator.addNode();
      mutator.addNode();
    }, tag: 'foo');

    expect(o.last.mutations.first.tags, equals(new Set<String>.from(['foo'])));
    expect(o.last.mutations.last.tags, equals(new Set<String>.from(['foo'])));
  });
}
