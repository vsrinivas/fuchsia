// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:doc_checker/graph.dart';
import 'package:test/test.dart';

void main() {
  group('doc_checker graph tests', () {
    test('getNode adds new node to graph', () {
      final Graph graph = Graph();
      expect(graph.nodeCount, equals(0));
      graph.getNode('label');
      expect(graph.nodeCount, equals(1));
    });

    test('getNode returns existing node', () {
      final Graph graph = Graph();
      final Node node1 = graph.getNode('label');
      final Node node2 = graph.getNode('label');
      expect(graph.nodeCount, equals(1));
      expect(node1, equals(node2));
    });

    test('no orphans with node connected to root', () {
      final Graph graph = Graph();
      final Node root = graph.getNode('root');
      graph.root = root;
      final Node node = graph.getNode('label');
      graph.addEdge(from: root, to: node);
      expect(graph.orphans, hasLength(0));
    });

    test('unknown node cannot be root', () {
      final Graph graph = Graph();
      final Node unknown = graph.getNode('unknown');
      expect(graph.nodeCount, equals(1));
      graph.removeSingletons();
      expect(graph.nodeCount, equals(0));
      expect(() => graph.root = unknown, throwsException);
    });
  });
}
