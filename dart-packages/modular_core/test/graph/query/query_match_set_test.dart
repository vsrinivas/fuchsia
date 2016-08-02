// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:test/test.dart';

import 'package:modular_core/graph/graph.dart';
import 'package:modular_core/graph/mem_graph.dart';
import 'package:modular_core/graph/query/query.dart';
import 'package:modular_core/graph/query/query_match_set.dart';
import 'package:modular_core/graph/test_utils/mutation_helper.dart';

class TestObserver {
  int calls = 0;

  void call() {
    calls++;
  }
}

void main() {
  MemGraph graph;
  MutationHelper helper;

  setUp(() {
    graph = new MemGraph();
    helper = new MutationHelper(graph);
  });
  test('Exposes matches as iterable', () {
    final GraphQuery query = new GraphQuery(['foo']);

    GraphQueryMatchSetImpl matchSet = new GraphQueryMatchSetImpl(graph, query);
    expect(matchSet.length, equals(0));
    expect(matchSet.isEmpty, isTrue);
    expect(matchSet.isNotEmpty, isFalse);

    Edge e = helper.addEdge(helper.addNode(), ['foo'], null);
    matchSet = new GraphQueryMatchSetImpl(graph, query);
    expect(matchSet.length, equals(1));
    expect(matchSet[0].rootNode, equals(e.target));
    expect(matchSet.first.rootNode, equals(e.target));
    expect(matchSet.single.rootNode, equals(e.target));
    expect(matchSet.isEmpty, isFalse);
    expect(matchSet.isNotEmpty, isTrue);
  });

  test('Observation', () {
    final GraphQuery query = new GraphQuery([
      'foo'
    ], childConstraints: [
      new GraphQuery(['bar'], isRequired: false)
    ]);

    GraphQueryMatchSetImpl matchSet = new GraphQueryMatchSetImpl(graph, query);
    expect(matchSet.length, equals(0));

    final TestObserver observer = new TestObserver();
    matchSet.addObserver(observer);

    // Add some nodes/edges that comprise a new match.
    Edge e1 = helper.addEdge(helper.addNode(), ['foo'], null);
    expect(observer.calls, equals(1));
    expect(matchSet.length, equals(1));

    // Add another one.
    helper.addEdge(helper.addNode(), ['foo'], null);
    expect(observer.calls, equals(2));
    expect(matchSet.length, equals(2));

    // Add a 'bar' edge off e1.target, and show that we don't see a notification
    // because it's not a new match.
    helper.addEdge(e1.target, ['bar'], null);
    expect(observer.calls, equals(2));
    expect(matchSet.length, equals(2));

    // Remove the observer and add a new set, and show it doesn't update.
    matchSet.removeObserver(observer);
    helper.addEdge(helper.addNode(), ['foo'], null);
    expect(observer.calls, equals(2));
    expect(matchSet.length, equals(2));
  });
}
