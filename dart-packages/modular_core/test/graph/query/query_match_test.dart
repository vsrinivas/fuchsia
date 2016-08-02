// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:typed_data';

import 'package:test/test.dart';

import 'package:modular_core/graph/graph.dart';
import 'package:modular_core/graph/mem_graph.dart';
import 'package:modular_core/graph/query/query.dart';
import 'package:modular_core/graph/test_utils/test_observer.dart';
import 'package:modular_core/graph/test_utils/mutation_helper.dart';

void expectMatch(GraphQueryMatch match, dynamic root,
    {List<Node> nodes, List<Edge> edges}) {
  List<Node> expectedNodes = new List<Node>();
  List<Edge> expectedEdges = new List<Edge>();

  if (root is Node) {
    expectedNodes.add(root);
  } else {
    // root is List
    expectedNodes.addAll(root);
  }
  if (nodes != null) expectedNodes.addAll(nodes);
  if (edges != null) {
    expectedEdges.addAll(edges);
    if (nodes == null) expectedNodes.addAll(edges.map((Edge e) => e.target));
  }
  if (root is Node) {
    expect(match.rootNode, equals(root));
    expect(match.rootNodes, equals([root]));
  } else {
    expect(match.rootNodes, equals(root));
  }
  expect(match.matchedNodes, equals(expectedNodes));
  expect(match.matchedEdges, equals(expectedEdges));
}

void main() {
  MemGraph graph;
  MutationHelper helper;
  Node nRoot;

  GraphQueryMatcher matcher;

  setUp(() {
    graph = new MemGraph();
    helper = new MutationHelper(graph);
    nRoot = helper.addNode();
    matcher = new GraphQueryMatcher(graph);
  });

  group('Single Node Base Cases:', () {
    test('Wildcard', () {
      // No labels means it's a wildcard.
      List<GraphQueryMatch> matches = matcher.match(new GraphQuery([]));

      // Wildcards match *all* nodes, and so the root node (created in setUp())
      // should match immediately.
      expect(matches.length, equals(1));
      expectMatch(matches[0], nRoot);

      Edge e1 = helper.addEdge(nRoot, ['foo'], null);
      Edge e2 = helper.addEdge(nRoot, ['bar'], null);
      Edge e3 = helper.addEdge(nRoot, [], null);

      matches = matcher.match(new GraphQuery([]));

      // Now we should see every node matched.
      expect(matches.length, equals(4));
      expectMatch(matches[0], nRoot);
      expectMatch(matches[1], e1.target);
      expectMatch(matches[2], e2.target);
      expectMatch(matches[3], e3.target);
    });

    test('Labels', () {
      Edge eBar = helper.addEdge(nRoot, ['bar'], null);
      Edge eBarBaz = helper.addEdge(nRoot, ['bar', 'baz'], null);

      List<GraphQueryMatch> matches = matcher.match(new GraphQuery(['bar']));
      // This should match 'bar' and 'bar baz' but not 'foo'.
      expect(matches.length, equals(2));
      expectMatch(matches[0], eBar.target);
      expectMatch(matches[1], eBarBaz.target);

      // Now put multiple labels into the match and show that only 'bar baz'
      // matched.
      matches = matcher.match(new GraphQuery(['bar', 'baz']));
      expect(matches.length, equals(1));
      expectMatch(matches[0], eBarBaz.target);
    });

    test('Values', () {
      Edge eFoo = helper.addEdge(nRoot, ['foo'], null);

      GraphQuery expr = new GraphQuery(['foo'], valueLabels: ['bar', 'bark']);
      expect(matcher.match(expr).isEmpty, isTrue);

      helper.setValue(eFoo.target, 'baz', [1]);
      expect(matcher.match(expr).isEmpty, isTrue);

      helper.setValue(eFoo.target, 'bar', [1]);
      List<GraphQueryMatch> matches = matcher.match(expr);
      expect(matches.length, equals(1));
      expectMatch(matches[0], eFoo.target);
    });

    test('Optional Children', () {
      // The GraphQuery indicates that it wants a 'foo' child, but it is
      // marked !isRequired, so the match still happens.
      List<GraphQueryMatch> matches =
          matcher.match(new GraphQuery([], childConstraints: [
        new GraphQuery(['foo'], isRequired: false)
      ]));
      expect(matches.length, equals(1));
      expectMatch(matches[0], nRoot);

      // If we mark it as required (the default), the match should come back
      // negative.
      matches = matcher.match(new GraphQuery([], childConstraints: [
        new GraphQuery(['foo'], isRequired: true)
      ]));
      expect(matches.length, equals(0));
    });
  });

  group('Repeated Root:', () {
    test('Basic', () {
      Edge eFoo1 = helper.addEdge(nRoot, ['foo'], null);
      Edge eFoo2 = helper.addEdge(nRoot, ['foo'], null);

      List<GraphQueryMatch> matches =
          matcher.match(new GraphQuery(['foo'], isRepeated: true));

      expect(matches.length, equals(1));
      expectMatch(matches[0], [eFoo1.target, eFoo2.target]);
    });
  });

  group('Children:', () {
    test('One Child', () {
      Edge eFoo = helper.addEdge(nRoot, ['foo'], null);
      Edge eBar = helper.addEdge(nRoot, ['bar'], null);

      // First try to match with a child 'bar' and show that we don't match.
      List<GraphQueryMatch> matches =
          matcher.match(new GraphQuery([], childConstraints: [
        new GraphQuery(['baz'], isRequired: true)
      ]));
      expect(matches.length, equals(0));

      // Now match 'foo' and we should get the full match back.
      matches = matcher.match(new GraphQuery([], childConstraints: [
        new GraphQuery(['foo'], isRequired: true)
      ]));
      expect(matches.length, equals(1));
      expectMatch(matches[0], nRoot, edges: [eFoo]);

      // If we say 'foo' isn't a required child, then we get three
      // matches: one from nRoot, and one on each child node, since
      // those match our wildcard, too.
      matches = matcher.match(new GraphQuery([], childConstraints: [
        new GraphQuery(['foo'], isRequired: false)
      ]));
      expect(matches.length, equals(3));
      expectMatch(matches[0], nRoot, edges: [eFoo]);
      expectMatch(matches[1], eFoo.target);
      expectMatch(matches[2], eBar.target);
    });

    test('Multiple Children', () {
      // Create a slightly more complex graph:
      //
      //   root ----> foo
      //       \----> bar ----> baz ----> bang
      //                 \----> baz
      helper.addEdge(nRoot, ['foo'], null);
      Edge eBar = helper.addEdge(nRoot, ['bar'], null);
      Edge eBaz1 = helper.addEdge(eBar.target, ['baz'], null);
      Edge eBang = helper.addEdge(eBaz1.target, ['bang'], null);
      Edge eBaz2 = helper.addEdge(eBar.target, ['baz'], null);

      // If we look for a match 'bar -> baz -> bang', with everything required,
      // we should get one match back.
      List<GraphQueryMatch> matches = matcher.match(new GraphQuery([
        'bar'
      ], childConstraints: [
        new GraphQuery([
          'baz'
        ], childConstraints: [
          new GraphQuery(['bang'], isRequired: true)
        ], isRequired: true)
      ]));
      expect(matches.length, equals(1));
      expectMatch(matches[0], eBar.target, edges: [eBaz1, eBang]);

      // However if 'bang' is not required, we should get two matches back.
      matches = matcher.match(new GraphQuery([
        'bar'
      ], childConstraints: [
        new GraphQuery([
          'baz'
        ], childConstraints: [
          new GraphQuery(['bang'], isRequired: false)
        ], isRequired: true)
      ]));
      expect(matches.length, equals(2));
      expectMatch(matches[0], eBar.target, edges: [eBaz1, eBang]);
      expectMatch(matches[1], eBar.target, edges: [eBaz2]);
    });

    test('Multiple Children: Cartesian Product', () {
      // Since we are still looking for just single node matches, when there
      // are multiple children, we have to create a match for each one. When
      // this applies to multiple different [childConstraints], we expect
      // a cartesian product of the matches.
      //
      //   root ----> foo
      //       \----> foo
      //       \----> bar
      //       \----> bar
      Edge eFoo1 = helper.addEdge(nRoot, ['foo'], null);
      Edge eFoo2 = helper.addEdge(nRoot, ['foo'], null);
      Edge eBar1 = helper.addEdge(nRoot, ['bar'], null);
      Edge eBar2 = helper.addEdge(nRoot, ['bar'], null);

      List<GraphQueryMatch> matches =
          matcher.match(new GraphQuery([], childConstraints: [
        new GraphQuery(['foo'], isRequired: true),
        new GraphQuery(['bar'], isRequired: true)
      ]));
      expect(matches.length, equals(4));
      expectMatch(matches[0], nRoot, edges: [eFoo1, eBar1]);
      expectMatch(matches[1], nRoot, edges: [eFoo1, eBar2]);
      expectMatch(matches[2], nRoot, edges: [eFoo2, eBar1]);
      expectMatch(matches[3], nRoot, edges: [eFoo2, eBar2]);
    });

    test('Multiple Children: Repeated', () {
      //   root ----> foo
      //       \----> foo
      //       \----> bar
      //       \----> bar
      Edge eFoo1 = helper.addEdge(nRoot, ['foo'], null);
      Edge eFoo2 = helper.addEdge(nRoot, ['foo'], null);
      Edge eBar1 = helper.addEdge(nRoot, ['bar'], null);
      Edge eBar2 = helper.addEdge(nRoot, ['bar'], null);

      List<GraphQueryMatch> matches =
          matcher.match(new GraphQuery([], childConstraints: [
        new GraphQuery(['foo'], isRequired: true, isRepeated: true),
        new GraphQuery(['bar'], isRequired: true)
      ]));

      expect(matches.length, equals(2));
      expectMatch(matches[0], nRoot, edges: [eFoo1, eFoo2, eBar1]);
      expectMatch(matches[1], nRoot, edges: [eFoo1, eFoo2, eBar2]);

      // This time set them both to repeated.
      matches = matcher.match(new GraphQuery([], childConstraints: [
        new GraphQuery(['foo'], isRequired: true, isRepeated: true),
        new GraphQuery(['bar'], isRequired: true, isRepeated: true)
      ]));

      expect(matches.length, equals(1));
      expectMatch(matches[0], nRoot, edges: [eFoo1, eFoo2, eBar1, eBar2]);

      // Add a third constraint but make it optional.
      matches = matcher.match(new GraphQuery([], childConstraints: [
        new GraphQuery(['foo'], isRequired: true, isRepeated: true),
        new GraphQuery(['bar'], isRequired: true, isRepeated: true),
        new GraphQuery(['baz'], isRequired: false, isRepeated: true)
      ]));

      expect(matches.length, equals(1));
      expectMatch(matches[0], nRoot, edges: [eFoo1, eFoo2, eBar1, eBar2]);
    });
  });

  group('Observation:', () {
    test('Nodes and Edges Stay Updated When Observed', () {
      Edge eFoo = helper.addEdge(nRoot, ['foo'], null);

      List<GraphQueryMatch> matches =
          matcher.match(new GraphQuery([], childConstraints: [
        new GraphQuery(['foo'], isRequired: true),
        new GraphQuery(['bar'], isRequired: false, isRepeated: true)
      ]));
      expect(matches.length, equals(1));
      expectMatch(matches[0], nRoot, edges: [eFoo]);

      // We should only stay updated when we're observed, so adding a Node that
      // would match should not do anything to the match.
      Edge eBar1 = helper.addEdge(nRoot, ['bar'], null);
      expectMatch(matches[0], nRoot, edges: [eFoo]);

      // So start observing!
      TestObserver observer = new TestObserver();
      matches[0].addObserver(observer);
      Edge eBar2 = helper.addEdge(nRoot, ['bar'], null);
      // And the same match instance should stay up-to-date.
      expectMatch(matches[0], nRoot, edges: [eFoo, eBar1, eBar2]);

      // Remove the Edge, show that it is removed from the match.
      helper.removeEdge(eBar2);
      expectMatch(matches[0], nRoot, edges: [eFoo, eBar1]);

      // Add something that *doesn't* match, just for good measure.
      helper.addEdge(nRoot, ['no matchy'], null);
      expectMatch(matches[0], nRoot, edges: [eFoo, eBar1]);

      // And finally remove the observer, and show that we stop updating.
      matches[0].removeObserver(observer);
      helper.addEdge(nRoot, ['bar'], null);
      expectMatch(matches[0], nRoot, edges: [eFoo, eBar1]);
    });

    test('Removing Match Root Leads to Empty Match', () {
      Node nFoo = helper.addNode();
      helper.addEdge(nRoot, ['foo'], nFoo);

      GraphQueryMatch fooMatch = matcher.match(new GraphQuery(['foo'])).first;
      expectMatch(fooMatch, nFoo);

      TestObserver observer = new TestObserver();
      fooMatch.addObserver(observer);

      helper.removeNode(fooMatch.rootNode);
      expect(fooMatch.matchedEdges, equals([]));
      expect(fooMatch.matchedNodes, equals([]));
    });

    test('Events: Filtering', () {
      // We want to show that GraphEvents are filtered to include only those
      // nodes/edges that are matched in the pattern.

      // Start with a simple graph, part of which will match our simple query:
      //
      //    root ---> foo
      //         \--> bar
      Edge eFoo = helper.addEdge(nRoot, ['foo'], null);
      Edge eBar = helper.addEdge(nRoot, ['bar'], null);

      GraphQueryMatch fooMatch = matcher
          .match(new GraphQuery([
            'foo'
          ], childConstraints: [
            new GraphQuery(['baz'], isRequired: false)
          ]))
          .single;

      final TestObserver o = new TestObserver();
      fooMatch.addObserver(o);

      // Now add a value to [eFoo.target], and we should see the event.
      helper.setValue(eFoo.target, 'valueKey', [0]);
      expect(o.events.length, equals(1));
      expect(
          o.events[0].mutations,
          equals([
            new GraphMutation(GraphMutationType.setValue,
                nodeId: eFoo.target.id,
                valueKey: 'valueKey',
                newValue: new Uint8List.fromList([0]))
          ]));

      // If we do the same for [eBar.target], we should see nothing.
      o.clear();
      helper.setValue(eBar.target, 'valueKey', [0]);
      expect(o.events.length, equals(0));

      // Add the optional 'baz' node, and see that we get notified.
      Edge eBaz = helper.addEdge(eFoo.target, ['baz'], null);

      expect(o.events.length, equals(1));
      expect(
          o.events[0].mutations,
          equals([
            new GraphMutation(GraphMutationType.addNode,
                nodeId: eBaz.target.id),
            new GraphMutation(GraphMutationType.addEdge,
                edgeId: eBaz.id,
                originNodeId: eBaz.origin.id,
                targetNodeId: eBaz.target.id,
                labels: eBaz.labels)
          ]));
    });

    test('Events: Synthesized When Nodes/Edges Are Updated', () {
      // Show that we create new GraphMutations to represent nodes and edges
      // that become suddenly 'visible' or hidden because of becoming included
      // in a pattern.

      // Create a simple graph:
      //
      //   root -> foo
      //
      // And later on create a disconnected graph that we hang off foo as the
      // last operation. We should see all the relevant events since these
      // nodes and edges will have been "created" from the standpoint of the
      // match.

      Edge eFoo = helper.addEdge(nRoot, ['foo'], null);

      // This query will match: foo -> bar? -> baz
      GraphQueryMatch fooMatch = matcher
          .match(new GraphQuery([
            'foo'
          ], childConstraints: [
            new GraphQuery(['bar'],
                isRequired: false,
                childConstraints: [
                  new GraphQuery(['baz'])
                ])
          ]))
          .single;

      final TestObserver o = new TestObserver();
      fooMatch.addObserver(o);

      // Create a node with a 'baz' edge.
      Node nBar = helper.addNode();
      Edge eBaz = helper.addEdge(nBar, ['baz'], null);

      // Since this sub-graph isn't yet connected to the 'foo' node, we
      // shouldn't see any of it in events yet.
      expect(o.events.length, equals(0));

      // However the moment we "connect" it to the matched sub-graph, we should
      // see events that imply these nodes were just added.
      Edge eBar;
      graph.mutate((GraphMutator mutator) {
        eBar = mutator.addEdge(eFoo.target.id, ['bar'], nBar.id);
      }, tag: 'tag');
      expect(o.events.length, equals(1));
      // We should also see the tag preserved.
      expect(o.events[0].mutations.first.tags.contains('tag'), equals(true));
      expect(
          o.events[0].mutations,
          equals([
            new GraphMutation(GraphMutationType.addNode, nodeId: nBar.id),
            new GraphMutation(GraphMutationType.addNode,
                nodeId: eBaz.target.id),
            new GraphMutation(GraphMutationType.addEdge,
                edgeId: eBaz.id,
                originNodeId: eBaz.origin.id,
                targetNodeId: eBaz.target.id,
                labels: eBaz.labels),
            new GraphMutation(GraphMutationType.addEdge,
                edgeId: eBar.id,
                originNodeId: eBar.origin.id,
                targetNodeId: eBar.target.id,
                labels: eBar.labels)
          ]));

      // Similarly if we remove the same edge, we should see the opposite
      // events.
      o.clear();
      graph.mutate((GraphMutator mutator) {
        mutator.removeEdge(eBar.id);
      });
      expect(o.events.length, equals(1));
      // We should also see the clientId preserved.
      expect(
          o.events[0].mutations,
          equals([
            new GraphMutation(GraphMutationType.removeNode, nodeId: nBar.id),
            new GraphMutation(GraphMutationType.removeNode,
                nodeId: eBaz.target.id),
            new GraphMutation(GraphMutationType.removeEdge,
                edgeId: eBaz.id,
                originNodeId: eBaz.origin.id,
                targetNodeId: eBaz.target.id,
                labels: eBaz.labels),
            new GraphMutation(GraphMutationType.removeEdge,
                edgeId: eBar.id,
                originNodeId: eBar.origin.id,
                targetNodeId: eBar.target.id,
                labels: eBar.labels)
          ]));
    }, skip: 'Gabe is not convinced this is needed');
  });
}
