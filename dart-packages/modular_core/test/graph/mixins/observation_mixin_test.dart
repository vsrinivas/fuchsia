// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:test/test.dart';

import 'package:modular_core/graph/graph.dart';
import 'package:modular_core/graph/mixins/observation_mixin.dart';
import 'package:modular_core/graph/test_utils/test_observer.dart';
import 'package:modular_core/graph/test_utils/fake_graph.dart';

class TestMixin extends GraphObservationMixin {
  FakeGraph baseGraph = new FakeGraph();

  Graph get mixinGraph => baseGraph;
}

void main() {
  TestMixin mixin;

  setUp(() {
    mixin = new TestMixin();
  });

  test('Add/Remove Observers', () {
    final TestObserver observer1 = new TestObserver();
    final TestObserver observer2 = new TestObserver();

    final GraphMutation mutation =
        new GraphMutation.addNode(new NodeId.fromString('one'));

    // Null case.
    mixin.notifyGraphChanged([mutation]);
    expect(observer1.calls, equals(0));
    expect(observer2.calls, equals(0));

    mixin.addObserver(observer1);
    mixin.notifyGraphChanged([mutation]);
    expect(observer1.calls, equals(1));
    expect(observer2.calls, equals(0));

    mixin.addObserver(observer2);
    mixin.notifyGraphChanged([mutation]);
    expect(observer1.calls, equals(2));
    expect(observer2.calls, equals(1));

    mixin.removeObserver(observer1);
    mixin.notifyGraphChanged([mutation]);
    expect(observer1.calls, equals(2));
    expect(observer2.calls, equals(2));
  });

  test('Event makeup', () {
    final TestObserver observer = new TestObserver();
    mixin.addObserver(observer);

    final GraphMutation mutation =
        new GraphMutation.addNode(new NodeId.fromString('one'));
    mixin.notifyGraphChanged([mutation]);
    expect(identical(observer.last.graph, mixin.baseGraph), isTrue);
    expect(observer.last.mutations.toList(), equals([mutation]));
  });

  test('Event aggregation', () {
    mixin.addObserver((final GraphEvent event) {
      if (event.mutations.length < 5) {
        List<GraphMutation> mutations = <GraphMutation>[];
        for (int i = 0; i < event.mutations.length + 1; ++i) {
          mutations.add(new GraphMutation.addNode(
              new NodeId.fromString('$event.mutations.length $i')));
        }
        mixin.notifyGraphChanged(mutations);
      }
    });
    final TestObserver observer = new TestObserver();
    mixin.addObserver(observer);
    mixin.notifyGraphChanged(
        [new GraphMutation.addNode(new NodeId.fromString('0'))]);

    expect(observer.events.length, equals(1));
    expect(observer.events[0].mutations.length, equals(15));
  });

  test('GraphMutation annotation tags', () {
    final GraphMutation mutation1 =
        new GraphMutation.addNode(new NodeId.fromString('one'));
    final GraphMutation mutation2 =
        new GraphMutation.addNode(new NodeId.fromString('two'));

    final TestObserver observer = new TestObserver();
    mixin.addObserver(observer);
    mixin.notifyGraphChanged([mutation1, mutation2], tag: 'foo');

    expect(observer.events.length, equals(1));
    expect(observer.last.mutations.length, equals(2));
    expect(observer.last.mutations.toList()[0].tags,
        equals(new Set<String>.from(['foo'])));
    expect(observer.last.mutations.toList()[1].tags,
        equals(new Set<String>.from(['foo'])));

    // If we notify again with a new tag we should see both tags.
    mixin.notifyGraphChanged(observer.last.mutations.toList(), tag: 'bar');
    expect(observer.last.mutations.length, equals(2));
    expect(observer.last.mutations.toList()[0].tags,
        equals(new Set<String>.from(['foo', 'bar'])));
    expect(observer.last.mutations.toList()[1].tags,
        equals(new Set<String>.from(['foo', 'bar'])));
  });
}
