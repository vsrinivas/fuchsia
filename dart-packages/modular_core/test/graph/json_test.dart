// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:convert' show BASE64, UTF8;
import 'dart:typed_data';

import 'package:test/test.dart';

import 'package:modular_core/graph/graph.dart';
import 'package:modular_core/graph/buffering_mutator.dart';
import 'package:modular_core/graph/test_utils/fake_graph.dart';
import 'package:modular_core/graph/mem_graph.dart';

void main() {
  group('Mutations', () {
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

    List<dynamic> mutationsJson() =>
        m.mutations.map((GraphMutation mu) => mu.toJson()).toList();

    test('addNode', () {
      final Node n = m.addNode();
      expect(
          mutationsJson(),
          equals([
            {'type': 'addNode', 'node': n.id.toString()}
          ]));

      expect(new GraphMutation.fromJson({'type': 'addNode', 'node': 'abc'}),
          equals(new GraphMutation.addNode(new NodeId.fromString('abc'))));

      expect(() => new GraphMutation.fromJson({'type': 'addNode'}),
          throwsA(isArgumentError));
      expect(
          () => new GraphMutation.fromJson({
                'type': 'addNode',
                'node': [1, 2, 3]
              }),
          throwsA(isArgumentError));
    });

    test('removeNode', () {
      final Node n = m.addNode();
      m.removeNode(n.id);
      expect(
          mutationsJson(),
          equals([
            {'type': 'addNode', 'node': n.id.toString()},
            {'type': 'removeNode', 'node': n.id.toString()},
          ]));

      expect(new GraphMutation.fromJson({'type': 'removeNode', 'node': 'abc'}),
          equals(new GraphMutation.removeNode(new NodeId.fromString('abc'))));

      expect(() => new GraphMutation.fromJson({'type': 'removeNode'}),
          throwsA(isArgumentError));
    });

    test('addEdge', () {
      final Node n1 = m.addNode();
      final Node n2 = m.addNode();
      final Edge e = m.addEdge(n1.id, ['foo', 'bar'], n2.id);

      expect(
          mutationsJson(),
          equals([
            {'type': 'addNode', 'node': n1.id.toString()},
            {'type': 'addNode', 'node': n2.id.toString()},
            {
              'type': 'addEdge',
              'edge': e.id.toString(),
              'origin': n1.id.toString(),
              'target': n2.id.toString(),
              'labels': ['foo', 'bar']
            }
          ]));

      expect(
          new GraphMutation.fromJson({
            'type': 'addEdge',
            'edge': 'xyz',
            'origin': 'abc',
            'target': 'def',
            'labels': ['foo', 'bar']
          }),
          equals(new GraphMutation.addEdge(
              new EdgeId.fromString('xyz'),
              new NodeId.fromString('abc'),
              new NodeId.fromString('def'),
              ['foo', 'bar'])));
      expect(
          () => new GraphMutation.fromJson({
                'type': 'addEdge',
                'origin': 'abc',
                'target': 'def',
                'labels': ['foo', 'bar']
              }),
          throwsA(isArgumentError));
      expect(
          () => new GraphMutation.fromJson({
                'type': 'addEdge',
                'edge': 'xyz',
                'origin': 'abc',
                'target': 'def',
              }),
          throwsA(isArgumentError));
      expect(
          () => new GraphMutation.fromJson({
                'type': 'addEdge',
                'edge': 'xyz',
                'target': 'def',
                'labels': ['foo', 'bar']
              }),
          throwsA(isArgumentError));
      expect(
          () => new GraphMutation.fromJson({
                'type': 'addEdge',
                'edge': 'xyz',
                'origin': 'abc',
                'target': 'def',
                'labels': 'hello'
              }),
          throwsA(isArgumentError));
      expect(
          () => new GraphMutation.fromJson({
                'type': 'addEdge',
                'edge': 'xyz',
                'origin': 'abc',
                'target': 'def',
                'labels': ['foo', 'bar', 666]
              }),
          throwsA(isArgumentError));
    });

    test('removeEdge', () {
      final Node n1 = m.addNode();
      final Node n2 = m.addNode();
      final Edge e = m.addEdge(n1.id, ['foo', 'bar'], n2.id);
      m.removeEdge(e.id);

      expect(
          mutationsJson(),
          equals([
            {'type': 'addNode', 'node': n1.id.toString()},
            {'type': 'addNode', 'node': n2.id.toString()},
            {
              'type': 'addEdge',
              'edge': e.id.toString(),
              'origin': n1.id.toString(),
              'target': n2.id.toString(),
              'labels': ['foo', 'bar']
            },
            {
              'type': 'removeEdge',
              'edge': e.id.toString(),
              'origin': n1.id.toString(),
              'target': n2.id.toString(),
              'labels': ['foo', 'bar']
            }
          ]));

      expect(
          new GraphMutation.fromJson({
            'type': 'removeEdge',
            'edge': 'xyz',
            'origin': 'abc',
            'target': 'def',
            'labels': ['foo', 'bar']
          }),
          equals(new GraphMutation.removeEdge(
              new EdgeId.fromString('xyz'),
              new NodeId.fromString('abc'),
              new NodeId.fromString('def'),
              ['foo', 'bar'])));
      expect(
          () => new GraphMutation.fromJson({
                'type': 'removeEdge',
                'origin': 'abc',
                'target': 'def',
                'labels': ['foo', 'bar']
              }),
          throwsA(isArgumentError));
      expect(
          () => new GraphMutation.fromJson({
                'type': 'removeEdge',
                'edge': 'xyz',
                'origin': 'abc',
                'target': 'def',
              }),
          throwsA(isArgumentError));
      expect(
          () => new GraphMutation.fromJson({
                'type': 'removeEdge',
                'edge': 'xyz',
                'target': 'def',
                'labels': ['foo', 'bar']
              }),
          throwsA(isArgumentError));
      expect(
          () => new GraphMutation.fromJson({
                'type': 'removeEdge',
                'edge': 'xyz',
                'origin': 'abc',
                'target': 'def',
                'labels': 'hello'
              }),
          throwsA(isArgumentError));
      expect(
          () => new GraphMutation.fromJson({
                'type': 'removeEdge',
                'edge': 'xyz',
                'origin': 'abc',
                'target': 'def',
                'labels': ['foo', 'bar', 666]
              }),
          throwsA(isArgumentError));
    });

    test('setValue', () {
      final Uint8List testBytes = new Uint8List.fromList([1, 2, 3, 4]);
      final String testBase64 = BASE64.encode(testBytes);

      final Node n = m.addNode();
      m.setValue(n.id, 'somekey', testBytes);
      m.setValue(n.id, 'somekey', null);

      expect(
          mutationsJson(),
          equals([
            {'type': 'addNode', 'node': n.id.toString()},
            {
              'type': 'setValue',
              'node': n.id.toString(),
              'key': 'somekey',
              'value': testBase64
            },
            {'type': 'setValue', 'node': n.id.toString(), 'key': 'somekey'},
          ]));

      expect(
          new GraphMutation.fromJson({
            'type': 'setValue',
            'node': 'abc',
            'key': 'somekey',
            'value': testBase64
          }),
          equals(new GraphMutation.setValue(
              new NodeId.fromString('abc'), 'somekey', testBytes)));

      expect(
          new GraphMutation.fromJson(
              {'type': 'setValue', 'node': 'abc', 'key': 'somekey'}),
          equals(new GraphMutation.setValue(
              new NodeId.fromString('abc'), 'somekey', null)));

      expect(
          () => new GraphMutation.fromJson(
              {'type': 'setValue', 'key': 'k', 'value': testBase64}),
          throwsA(isArgumentError));

      expect(
          () => new GraphMutation.fromJson(
              {'type': 'setValue', 'node': 'abc', 'value': testBase64}),
          throwsA(isArgumentError));
      expect(
          () => new GraphMutation.fromJson({
                'type': 'setValue',
                'node': 'abc',
                'key': ['not', 'a', 'string'],
                'value': testBase64
              }),
          throwsA(isArgumentError));
      expect(
          () => new GraphMutation.fromJson(
              {'type': 'setValue', 'node': 'abc', 'key': 'k', 'value': null}),
          throwsA(isArgumentError));
      expect(
          () => new GraphMutation.fromJson({
                'type': 'setValue',
                'node': 'abc',
                'key': 'k',
                'value': 'invalid base64'
              }),
          throwsA(isArgumentError));
    });

    test('Bad type', () {
      expect(() => new GraphMutation.fromJson({}), throwsA(isArgumentError));
      expect(() => new GraphMutation.fromJson({'type': 'garbage'}),
          throwsA(isArgumentError));
      expect(
          () => new GraphMutation.fromJson({
                'type': {'not': 'a string'}
              }),
          throwsA(isArgumentError));
    });
  });

  group('Graph', () {
    Graph graph;
    setUp(() {
      graph = new MemGraph(prefix: 'test');
    });

    test('Empty graph', () {
      expect(graph.toJson(), equals({'nodes': [], 'edges': []}));
    });

    test('Simple Graph', () {
      Node n1, n2, n3;
      Edge e1, e2, e3;
      final Uint8List v = new Uint8List.fromList(UTF8.encode("Hello, world"));
      final String v64 = BASE64.encode(v);
      graph.mutate((GraphMutator m) {
        n1 = m.addNode();
        n2 = m.addNode();
        n3 = m.addNode();
        e1 = m.addEdge(n1.id, ['one'], n2.id);
        e2 = m.addEdge(n2.id, ['two', 'deux'], n3.id);
        e3 = m.addEdge(n3.id, [], n1.id);
        m.setValue(n1.id, "value-key", v);
      });

      final dynamic json = graph.toJson();
      expect(json, new isInstanceOf<Map>());
      expect(json.keys, unorderedEquals(['nodes', 'edges']));

      expect(
          json['nodes'],
          unorderedEquals([
            {
              'id': n1.id.toString(),
              'values': {'value-key': v64}
            },
            {'id': n2.id.toString(), 'values': {}},
            {'id': n3.id.toString(), 'values': {}},
          ]));

      expect(
          json['edges'],
          unorderedEquals([
            {
              'id': e1.id.toString(),
              'origin': n1.id.toString(),
              'target': n2.id.toString(),
              'labels': ['one']
            },
            {
              'id': e2.id.toString(),
              'origin': n2.id.toString(),
              'target': n3.id.toString(),
              'labels': ['two', 'deux']
            },
            {
              'id': e3.id.toString(),
              'origin': n3.id.toString(),
              'target': n1.id.toString(),
              'labels': []
            },
          ]));
    });
  });
}
