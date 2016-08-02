// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:async';
import 'dart:typed_data';

import 'package:handler/bindings.dart';
import 'package:handler/graph/impl/fake_ledger_graph_store.dart';
import 'package:handler/graph/impl/session_graph_impl.dart';
import 'package:handler/graph/impl/session_graph_link_impl.dart';
import 'package:handler/graph/session_graph.dart';
import 'package:handler/graph/session_graph_store.dart';
import 'package:modular_core/graph/mem_graph.dart';
import 'package:modular_core/graph/test_utils/mutation_helper.dart';
import 'package:modular_core/graph/test_utils/test_observer.dart';
import 'package:modular_core/graph/query/query.dart';
import 'package:modular_core/uuid.dart';
import 'package:test/test.dart';

void main() {
  group('SessionGraphLink:', () {
    test('Serialization and Equality', () {
      MemGraph graph = new MemGraph();
      Node root, origin;
      graph.mutate((GraphMutator mutator) {
        root = mutator.addNode();
        origin = mutator.addNode();
      });

      final SerializableSessionGraphLink link = new SessionGraphLink(
          new Uuid.random(),
          new GraphQuery(['foo'],
              isRepeated: true,
              isRequired: false,
              valueLabels: ['bar'],
              childConstraints: [
                new GraphQuery(['baz'])
              ]),
          origin,
          ['qux']);

      expect(graph.edges.length, equals(0));
      expect(graph.nodes.length, equals(2));

      link.writeGraphMetadata(graph, root);

      expect(graph.edges.length, equals(2));
      expect(graph.nodes.length, equals(3));
      expect(origin.inEdges.length, equals(1));

      final Set<SessionGraphLink> links =
          SerializableSessionGraphLink.fromGraphMetadata(graph);
      expect(links.length, equals(1));
      expect(link, equals(links.first));
    });
  });

  group('Session Graph:', () {
    FakeLedgerGraphStore innerStore;
    SessionGraphStore store;
    SessionGraphImpl session1;
    SessionGraphImpl session2;
    SessionGraphImpl session3;
    SessionGraphImpl session4;
    MemGraph ledger1;
    MemGraph ledger2;
    MemGraph ledger3;
    MemGraph ledger4;

    // Helper function to find the exposed node in session1.
    // The node is expected to be present.
    Node exposedNode(String id) {
      final Node node = session1.node(new NodeId.fromString(id));
      assert(node != null);
      return node;
    }

    setUp(() async {
      int ledgerGraphCount = 0;
      int sessionGraphCount = 0;
      innerStore = new FakeLedgerGraphStore(
          prefixGenerator: ((_) => 'ledger-${++ledgerGraphCount}'));
      store = new SessionGraphStore(innerStore,
          prefixGenerator: ((_) {
            assert(ledgerGraphCount == sessionGraphCount + 1);
            return 'sess-${++sessionGraphCount}';
          }));
      session1 = await store.createGraph(new Uuid.random());
      session2 = await store.createGraph(new Uuid.random());
      session3 = await store.createGraph(new Uuid.random());
      session4 = await store.createGraph(new Uuid.random());
      ledger1 = session1.baseGraph;
      ledger2 = session2.baseGraph;
      ledger3 = session3.baseGraph;
      ledger4 = session4.baseGraph;
    });

    test('Simple Read/Write', () {
      // Non-internal edges/properties values in the ledger graph are
      // visible in the session graph.
      MutationHelper helper = new MutationHelper(ledger1);
      final Node n1 = helper.addNode();
      final Node n2 = helper.addNode();
      final Node n3 = helper.addNode();
      final Node n4 = helper.addNode();
      final Node n5 = helper.addNode();
      final Edge e1 = helper.addEdge(n1, ['foo', 'bar'], n2);
      final Edge e2 = helper.addEdge(n2, ['foo', 'bar'], n3);
      final Edge e3 = helper.addEdge(n3, ['foo', 'bar'], n4);
      final Edge e4 = helper.addEdge(n4, ['foo', 'bar'], n5);

      final Uint8List val = new Uint8List.fromList([1, 2, 3, 4, 5, 6]);
      helper.setValue(n2, 'qux', val);
      expect(session1.node(n1.id), isNotNull);
      expect(session1.node(n2.id), isNotNull);
      expect(session1.edge(e1.id), isNotNull);
      expect(session1.node(n2.id).getValue('qux'), equals(val));

      // Removing nodes/edges from the "ledger" is reflected in the "session".
      helper.removeNode(n1);
      expect(session1.node(n1.id), isNull);
      expect(ledger1.node(n1.id), isNull);
      expect(session1.edge(e1.id), isNull);
      expect(ledger1.edge(e1.id), isNull);
      expect(session1.node(n2.id), isNotNull);
      expect(ledger1.node(n2.id), isNotNull);
      expect(session1.edge(e2.id), isNotNull);
      expect(ledger1.edge(e2.id), isNotNull);

      // Conversely, removing them from the "session" also removes them
      // in the "ledger".
      MutationHelper session1helper = new MutationHelper(session1);
      session1helper.setValue(session1.node(n2.id), 'qux', null);
      expect(ledger1.node(n2.id).getValue('qux'), isNull);
      session1helper.removeNode(session1.node(n2.id));
      session1helper.removeEdge(session1.edge(e3.id));
      expect(session1.node(n2.id), isNull);
      expect(ledger1.node(n2.id), isNull);
      expect(session1.edge(e2.id), isNull);
      expect(ledger1.edge(e2.id), isNull);
      expect(session1.edge(e3.id), isNull);
      expect(ledger1.edge(e3.id), isNull);
      expect(session1.node(n5.id), isNotNull);
      expect(ledger1.node(n5.id), isNotNull);
      expect(session1.edge(e4.id), isNotNull);
      expect(ledger1.edge(e4.id), isNotNull);
    });

    test('Observe Changes', () {
      TestObserver observer = new TestObserver();
      session1.addObserver(observer);

      MutationHelper helper = new MutationHelper(ledger1);
      Node n1 = helper.addNode(id: 'n1');
      expect(observer.last.graph, same(session1));
      expect(
          observer.last.mutations, equals([new GraphMutation.addNode(n1.id)]));

      Node n2 = helper.addNode(id: 'n2');
      expect(observer.last.graph, same(session1));
      expect(
          observer.last.mutations, equals([new GraphMutation.addNode(n2.id)]));

      GraphMutation mutation =
          new GraphMutation.addNode(new NodeId.fromString('n3'));
      session1.mutate((GraphMutator mutator) {
        mutator.apply(mutation);
      }, tag: 'foo');
      expect(observer.last.graph, same(session1));
      expect(observer.last.mutations, equals([mutation]));
      expect(observer.last.mutations.first.tags, equals(['foo']));

      Edge e = helper.addEdge(n1, ['label'], n2);
      expect(observer.last.graph, same(session1));
      expect(
          observer.last.mutations,
          equals([
            new GraphMutation.addEdge(e.id, n1.id, n2.id, ['label'])
          ]));
    });

    test('Internal Edges and Values Hidden', () {
      // Internal edges/properties are not exposed through the SessionGraph.
      MutationHelper helper = new MutationHelper(ledger1);
      final Node n1L1 = helper.addNode();
      final Node n2L1 = helper.addNode();
      final Edge e1L1 = helper.addEdge(n1L1, ['foo'], n2L1);
      final Edge e2L1 =
          helper.addEdge(n2L1, ['internal:session_graph:edge'], n1L1);
      final Uint8List val = new Uint8List.fromList([1, 2, 3, 4, 5, 6]);
      helper.setValue(n1L1, 'bar', val);
      helper.setValue(n2L1, 'internal:session_graph:foo', val);

      final Node n1G1 = session1.node(n1L1.id);
      final Node n2G1 = session1.node(n2L1.id);
      final Edge e1G1 = session1.edge(e1L1.id);
      final Edge e2G1 = session1.edge(e2L1.id);
      expect(n1G1, isNotNull);
      expect(n2G1, isNotNull);
      expect(e1G1, isNotNull);
      expect(e2G1, isNull);
      expect(n1G1.getValue('bar'), equals(val));
      expect(n2G1.getValue('internal:session_graph:foo'), isNull);
      expect(n1G1.outEdges.length, equals(1));
      expect(n2G1.inEdges.length, equals(1));
      expect(n1G1.inEdges.length, equals(0));
      expect(n2G1.outEdges.length, equals(0));
    });

    test('Display Edges are hidden', () {
      // TODO(jjosh): This is testing a hack in addEdge() that should
      // eventually be handled more generally, perhaps by exposing some notion
      // of ephemerality in the Graph API.
      MutationHelper helper = new MutationHelper(session1);
      final Node node1 = helper.addNode(id: 'displayNode');
      helper.setValue(
          node1, Binding.displayNodeLabel, new Uint8List.fromList([1]));
      final Edge edge1 = helper.addEdge(session1.root, ['card'], node1);

      expect(ledger1.edge(edge1.id), isNull);
      expect(ledger1.node(node1.id), isNull);
      expect(session1.edge(edge1.id), equals(edge1));
      expect(session1.node(node1.id), equals(node1));
    });

    test('Simple Linking', () async {
      // Used to create the same link multiple times, within this test.
      final Function createSessionLink = () async {
        final SessionGraphLink link =
            session1.addSessionLink(session2.sessionId,
                query: new GraphQuery([
                  'bar'
                ], childConstraints: [
                  new GraphQuery(['baz'])
                ]),
                linkOrigin: session1.node(new NodeId.fromString('n1_2')),
                labels: ['foo']);
        await new Future<Null>.microtask(() {});
        return link;
      };

      // We will be querying the "session1" graph to see the set of matches
      // change as we link/unlink other sessions' graphs, and make changes to
      // those graphs.
      final GraphQuery fooBarBaz = new GraphQuery([
        'foo'
      ], childConstraints: [
        new GraphQuery([
          'bar'
        ], childConstraints: [
          new GraphQuery(['baz'])
        ])
      ]);

      MutationHelper helper1 = new MutationHelper(ledger1);
      final Node n11 = helper1.addNode(id: 'n1_1');
      final Node n12 = helper1.addNode(id: 'n1_2');
      expect(session1.edges.length, equals(1));
      helper1.addEdge(n11, ['foo'], n12, id: 'e1_1');
      expect(session1.edges.length, equals(2));
      // There are no "foo->bar->baz" paths in "session1".
      expect(fooBarBaz.match(session1).length, equals(0));

      // We will link all "bar->baz" from "session2" into "session1".
      MutationHelper helper2 = new MutationHelper(ledger2);
      final Node n21 = helper2.addNode(id: 'n2_1');
      final Node n22 = helper2.addNode(id: 'n2_2');
      final Node n23 = helper2.addNode(id: 'n2_3');
      final Node n24 = helper2.addNode(id: 'n2_4');
      final Node n25 = helper2.addNode(id: 'n2_5');
      final Node n26 = helper2.addNode(id: 'n2_6');

      // Add one "bar->baz" link to "session2", and another partial "baz->bar".
      final Edge e21 = helper2.addEdge(n21, ['bar'], n22, id: 'e2_1');
      final Edge e22 = helper2.addEdge(n22, ['baz'], n23, id: 'e2_2');
      final Edge e23 = helper2.addEdge(n24, ['bar'], n25, id: 'e2_3');

      final SessionGraphLink link1 = await createSessionLink();

      // There is now 1 match, because a "foo" edge was added link "n1_2" to
      // the root of the "bar->baz" match, i.e. "n2_1".
      final List<GraphQueryMatch> match1 = fooBarBaz.match(session1);
      expect(match1.length, equals(1));
      // TODO(jjosh): GraphQueryMatch is super-inconvenient for this use-case.
      expect(match1.first.rootNode.inEdges.first.origin.id.toString(),
          equals('n1_2'));
      expect(exposedNode('n1_2').outEdges.length, equals(1));
      expect(exposedNode('n1_2').outEdges.first.labels, equals(['foo']));

      // We shouldn't see an edge from "n1_2" in ledger1, though, only in
      // session1.
      expect(n12.outEdges, equals([]));
      expect(session1.node(n12.id).outEdges.length, equals(1));

      // Adding another "bar" edge completes the second "bar->baz" match.
      expect(session1.edge(e23.id), isNull);
      final Edge e24 = helper2.addEdge(n25, ['baz'], n26, id: 'e2_4');
      expect(session1.edge(e23.id), isNotNull);
      final List<GraphQueryMatch> match2 = fooBarBaz.match(session1);
      expect(match2.length, equals(2));

      // Removing the link causes both matches to disappear, along with the
      // exposed edges.
      session1.removeSessionLink(link1);
      final List<GraphQueryMatch> match3 = fooBarBaz.match(session1);
      expect(match3.length, equals(0));
      expect(session1.edge(e21.id), isNull);
      expect(session1.edge(e22.id), isNull);
      expect(session1.edge(e23.id), isNull);
      expect(session1.edge(e24.id), isNull);
      expect(session1.node(n21.id), isNull);
      expect(session1.node(n22.id), isNull);
      expect(session1.node(n23.id), isNull);
      expect(session1.node(n24.id), isNull);
      expect(session1.node(n25.id), isNull);
      expect(session1.node(n26.id), isNull);

      // Adding the same link back causes the matches to reappear.
      await createSessionLink();

      final List<GraphQueryMatch> match4 = fooBarBaz.match(session1);
      expect(match4.length, equals(2));
      expect(session1.edge(e21.id) == null, equals(false));
      expect(session1.edge(e22.id) == null, equals(false));
      expect(session1.edge(e23.id) == null, equals(false));
      expect(session1.edge(e24.id) == null, equals(false));
      expect(session1.node(n21.id) == null, equals(false));
      expect(session1.node(n22.id) == null, equals(false));
      expect(session1.node(n23.id) == null, equals(false));
      expect(session1.node(n24.id) == null, equals(false));
      expect(session1.node(n25.id) == null, equals(false));
      expect(session1.node(n26.id) == null, equals(false));

      // Operations on session1 which persist to session2 work.
      Edge newBaz;
      session1.mutate((GraphMutator mutator) {
        final Node newNode = mutator.addNode();
        mutator.addEdge(n21.id, ['bar'], newNode.id);
        newBaz = mutator.addEdge(newNode.id, ['baz']);
      });
      expect(session1.edge(newBaz.id), isNotNull);
      expect(ledger1.edge(newBaz.id), isNull);
      expect(session2.edge(newBaz.id), isNotNull);
      expect(ledger2.edge(newBaz.id), isNotNull);
    });

    test('Link Multiple Graphs', () async {
      // Used to create the same link multiple times.
      final Function createSessionLink =
          (SessionGraphImpl sessionN, String linkLabel) async {
        final SessionGraphLink link = session1.addSessionLink(
            sessionN.sessionId,
            query: new GraphQuery(['leaf']),
            linkOrigin: session1.node(new NodeId.fromString('n1_2')),
            labels: [linkLabel]);
        await new Future<Null>.microtask(() {});
        return link;
      };

      final Function createLinkToSession2 =
          () async => createSessionLink(session2, 'branch1');
      final Function createLinkToSession3 =
          () async => createSessionLink(session3, 'branch2');
      final Function createLinkToSession4 =
          () async => createSessionLink(session4, 'branch3');

      // We will be querying the "session1" graph to see the set of matches
      // change as we link/unlink other sessions' graphs, and make changes to
      // those graphs.  We are looking for matches like:
      //   root --> branch1 --> leaf
      //        --> branch2 --> leaf
      //        --> branch3 --> leaf
      final GraphQuery treeQuery = new GraphQuery([
        'root'
      ], childConstraints: [
        new GraphQuery([
          'branch1'
        ], childConstraints: [
          new GraphQuery(['leaf'])
        ]),
        new GraphQuery([
          'branch2'
        ], childConstraints: [
          new GraphQuery(['leaf'])
        ]),
        new GraphQuery([
          'branch3'
        ], childConstraints: [
          new GraphQuery(['leaf'])
        ])
      ]);

      final MutationHelper helper1 = new MutationHelper(ledger1);
      final Node n11 = helper1.addNode(id: 'n1_1');
      final Node n12 = helper1.addNode(id: 'n1_2');
      expect(session1.edges.length, equals(1));
      helper1.addEdge(n11, ['root'], n12, id: 'e1_1');
      expect(session1.edges.length, equals(2));
      expect(treeQuery.match(session1).length, equals(0));

      SessionGraphLink sessionLink2;
      SessionGraphLink sessionLink3;
      SessionGraphLink sessionLink4;

      // Link immediately to session2.  There are no link matches, so the number
      // of edges shouldn't change.
      sessionLink2 = await createLinkToSession2();
      expect(session1.edges.length, equals(2));
      expect(treeQuery.match(session1).length, equals(0));

      // Add a 'leaf' edge to session2.  There should now be a 'branch1' and a
      // 'leaf' edge in session1.
      final MutationHelper helper2 = new MutationHelper(ledger2);
      final Node n21 = helper2.addNode(id: 'n2_1');
      final Node n22 = helper2.addNode(id: 'n2_2');
      helper2.addEdge(n21, ['leaf'], n22, id: 'e2_1');
      expect(session1.edges.where((Edge e) => e.labels.contains('leaf')).length,
          equals(1));
      expect(
          session1.edges.where((Edge e) => e.labels.contains('branch1')).length,
          equals(1));
      expect(treeQuery.match(session1).length, equals(0));

      // This time, add a 'leaf' edge to session3 before linking to the session.
      final MutationHelper helper3 = new MutationHelper(ledger3);
      final Node n31 = helper3.addNode(id: 'n3_1');
      final Node n32 = helper3.addNode(id: 'n3_2');
      helper3.addEdge(n31, ['leaf'], n32, id: 'e3_1');
      expect(
          session1.edges.where((Edge e) => e.labels.contains('branch2')).length,
          equals(0));
      sessionLink3 = await createLinkToSession3();
      expect(
          session1.edges.where((Edge e) => e.labels.contains('branch2')).length,
          equals(1));

      // After adding a 'leaf' and linking to session4, there is now a treeQuery
      // match.
      final MutationHelper helper4 = new MutationHelper(ledger4);
      final Node n41 = helper4.addNode(id: 'n4_1');
      final Node n42 = helper4.addNode(id: 'n4_2');
      helper4.addEdge(n41, ['leaf'], n42, id: 'e4_1');
      sessionLink4 = await createLinkToSession4();
      expect(treeQuery.match(session1).length, equals(1));

      // Removing one link causes the treeQuery match to disappear.
      session1.removeSessionLink(sessionLink2);
      expect(treeQuery.match(session1).length, equals(0));

      // Adding it back causes the treeQuery match to reappear.
      sessionLink2 = await createLinkToSession2();
      expect(treeQuery.match(session1).length, equals(1));

      // Removing all three causes the treeQuery match to disappear.
      session1.removeSessionLink(sessionLink2);
      session1.removeSessionLink(sessionLink3);
      session1.removeSessionLink(sessionLink4);
      expect(treeQuery.match(session1).length, equals(0));

      // Adding back all three causes the treeQuery match to reappear.
      sessionLink2 = await createLinkToSession2();
      sessionLink3 = await createLinkToSession3();
      sessionLink4 = await createLinkToSession4();
      expect(treeQuery.match(session1).length, equals(1));
    });

    test('Transitive Linking', () async {
      // ledger1 will contain an 'A' edge.
      // ledger2 will contain a pair of edges 'C->D'.
      // ledger3 will contain a 'F' edge.
      // ledger4 will contain a 'H' edge.
      final GraphQuery queryFGH = new GraphQuery([
        'F'
      ], childConstraints: [
        new GraphQuery([
          'G'
        ], childConstraints: [
          new GraphQuery(['H'])
        ])
      ]);

      final GraphQuery queryCDEFGH = new GraphQuery([
        'C'
      ], childConstraints: [
        new GraphQuery([
          'D'
        ], childConstraints: [
          new GraphQuery(['E'], childConstraints: [queryFGH])
        ])
      ]);

      final GraphQuery queryABCDEFGH = new GraphQuery([
        'A'
      ], childConstraints: [
        new GraphQuery(['B'], childConstraints: [queryCDEFGH])
      ]);

      final MutationHelper helper1 = new MutationHelper(ledger1);
      final Node n11 = helper1.addNode(id: 'n1_1');
      final Node n12 = helper1.addNode(id: 'n1_2');
      helper1.addEdge(n11, ['A'], n12, id: 'e1_1');

      final MutationHelper helper2 = new MutationHelper(ledger2);
      final Node n21 = helper2.addNode(id: 'n2_1');
      final Node n22 = helper2.addNode(id: 'n2_2');
      final Node n23 = helper2.addNode(id: 'n2_3');
      final Edge e21 = helper2.addEdge(n21, ['C'], n22, id: 'e2_1');
      helper2.addEdge(n22, ['D'], n23, id: 'e2_2');

      final MutationHelper helper3 = new MutationHelper(ledger3);
      final Node n31 = helper3.addNode(id: 'n3_1');
      final Node n32 = helper3.addNode(id: 'n3_2');
      helper3.addEdge(n31, ['F'], n32, id: 'e3_1');

      final MutationHelper helper4 = new MutationHelper(ledger4);
      final Node n41 = helper4.addNode(id: 'n4_1');
      final Node n42 = helper4.addNode(id: 'n4_2');
      helper4.addEdge(n41, ['H'], n42, id: 'e4_1');

      expect(queryABCDEFGH.match(session1).length, equals(0));
      expect(queryCDEFGH.match(session2).length, equals(0));
      expect(queryFGH.match(session3).length, equals(0));

      session3.addSessionLink(session4.sessionId,
          query: new GraphQuery(['H']),
          linkOrigin: session3.node(new NodeId.fromString('n3_2')),
          labels: ['G']);
      await new Future<Null>.microtask(() {});

      // New edges are visible in session3, so queryFGH matches.
      expect(queryABCDEFGH.match(session1).length, equals(0));
      expect(queryCDEFGH.match(session2).length, equals(0));
      expect(queryFGH.match(session3).length, equals(1));

      final SessionGraphLink link12 = session1.addSessionLink(
          session2.sessionId,
          query: queryCDEFGH,
          linkOrigin: session1.node(new NodeId.fromString('n1_2')),
          labels: ['B']);
      await new Future<Null>.microtask(() {});

      // No matches yet.
      expect(queryABCDEFGH.match(session1).length, equals(0));
      expect(queryCDEFGH.match(session2).length, equals(0));
      expect(queryFGH.match(session3).length, equals(1));

      session2.addSessionLink(session3.sessionId,
          query: queryFGH,
          linkOrigin: session2.node(new NodeId.fromString('n2_3')),
          labels: ['E']);
      await new Future<Null>.microtask(() {});

      // Woo-hoo!  Transitive linking observed!
      expect(queryCDEFGH.match(session2).length, equals(1));
      expect(queryABCDEFGH.match(session1).length, equals(1));

      // Removing a link causes the full match to disappear, and re-adding it
      // causes it to reappear.
      session1.removeSessionLink(link12);
      expect(queryABCDEFGH.match(session1).length, equals(0));
      session1.addSessionLink(session2.sessionId,
          query: queryCDEFGH,
          linkOrigin: session1.node(new NodeId.fromString('n1_2')),
          labels: ['B']);
      await new Future<Null>.microtask(() {});
      expect(queryABCDEFGH.match(session1).length, equals(1));

      // Removing an edge causes the full match to disappear, and re-adding it
      // causes it to reappear.
      helper2.removeEdge(e21);
      expect(queryABCDEFGH.match(session1).length, equals(0));
      helper2.addEdge(n21, ['C'], n22);
      expect(queryABCDEFGH.match(session1).length, equals(1));
    });

    // An edge added between two already-persisted nodes is immediately
    // persisted, but an edge added between unpersisted nodes is not.
    test('Lazy Persistence 1', () {
      // Add two persisted nodes.
      final MutationHelper helper1 = new MutationHelper(ledger1);
      final Node origin = helper1.addNode(id: 'ORIGIN');
      final Node sOrigin = session1.node(origin.id);
      expect(sOrigin, isNotNull);
      final Node target = helper1.addNode(id: 'TARGET');
      final Node sTarget = session1.node(target.id);
      expect(sTarget, isNotNull);

      // Edge will be persisted immediately.
      final Edge edge = new MutationHelper(session1)
          .addEdge(sOrigin, ['foo'], sTarget, id: 'EDGE');
      expect(ledger1.edge(edge.id), isNotNull);
    });

    // An edge added between two already-persisted nodes is immediately
    // persisted, but an edge added between unpersisted nodes is not.
    test('Lazy Persistence 2', () {
      // Add two unpersisted nodes.
      final MutationHelper sHelper2 = new MutationHelper(session2);
      final Node sOrigin = sHelper2.addNode(id: 'ORIGIN');
      expect(ledger2.node(sOrigin.id), isNull);
      final Node sTarget = sHelper2.addNode(id: 'TARGET');
      expect(ledger2.node(sTarget.id), isNull);

      // Edge will not be persisted immediately.
      final Edge sEdge =
          sHelper2.addEdge(sOrigin, ['foo'], sTarget, id: 'EDGE');
      expect(ledger2.edge(sEdge.id), isNull);

      // Edge is persisted when its origin is pointed at by a persisted edge.
      final Node persisted =
          new MutationHelper(ledger2).addNode(id: 'PERSISTED');
      sHelper2.addEdge(session2.node(persisted.id), ['foo'], sOrigin);
      expect(ledger2.edge(sEdge.id), isNotNull);
    });

    // Verify that unrooted nodes/edges added to a session graph are not
    // immediately persisted.  Later, if a edge is added to an unpersisted node
    // from a persisted node, the former will be persisted in the same graph as
    // the latter.  Any unpersisted outbound edges of the node will also be
    // persisted; by transitivity, this results in the persistence of the entire
    // unpersisted subgraph.
    test('Transitive Lazy Persistence', () async {
      MutationHelper helper1 = new MutationHelper(ledger1);
      final Node ledger1Root = helper1.addNode(id: 'ledger1Root');
      MutationHelper helper2 = new MutationHelper(ledger2);
      final Node ledger2Root = helper2.addNode(id: 'ledger2Root');
      // To make 'ledgerRoot2' be matched and show up in session1.
      helper2.addEdge(helper2.addNode(), ['ledger2RootEdge'], ledger2Root);

      List<NodeId> createBinaryTreeRootedAt(
          GraphMutator mutator, final Node root, final int depth) {
        assert(root != null && depth >= 1);
        final List<NodeId> list = <NodeId>[root.id];
        mutator.setValue(root.id, 'depth', new Uint8List.fromList([depth]));
        if (depth > 1) {
          final Node left = mutator.addNode();
          mutator.addEdge(root.id, [depth > 2 ? 'branch' : 'leaf'], left.id);
          list.addAll(createBinaryTreeRootedAt(mutator, left, depth - 1));

          final Node right = mutator.addNode();
          mutator.addEdge(root.id, [depth > 2 ? 'branch' : 'leaf'], right.id);
          list.addAll(createBinaryTreeRootedAt(mutator, right, depth - 1));
        }
        return list;
      }

      session1.addSessionLink(session2.sessionId,
          query: new GraphQuery(['ledger2RootEdge']),
          linkOrigin: null,
          labels: ['']);

      await new Future<Null>.microtask(() {});

      // Now that session2 is linked into session1, we should be able to find
      // both nodes exposed in session1.
      final Node session1Root = session1.node(ledger1Root.id);
      final Node session2Root = session1.node(ledger2Root.id);
      expect(session1Root, isNotNull);
      expect(session2Root, isNotNull);

      // Create 2 trees.  Because they are not linked to by any persisted edges,
      // they do not appear in ledger1.
      MutationHelper sHelper1 = new MutationHelper(session1);
      final List<NodeId> tree1 = <NodeId>[];
      final List<NodeId> tree2 = <NodeId>[];
      final Node treeRoot1 = sHelper1.addNode(id: 'treeRoot1');
      final Node treeRoot2 = sHelper1.addNode(id: 'treeRoot2');
      session1.mutate((GraphMutator mutator) {
        tree1.addAll(createBinaryTreeRootedAt(mutator, treeRoot1, 4));
        tree2.addAll(createBinaryTreeRootedAt(mutator, treeRoot2, 4));
      });
      expect(tree1.length, equals(15));
      expect(tree2.length, equals(15));
      expect(session1.nodes.length, greaterThan(tree1.length + tree2.length));
      expect(ledger1.nodes.length, lessThan(10));
      expect(ledger2.nodes.length, lessThan(10));
      expect(
          tree1.any((NodeId node) => session1.getNodePersistor(node) != null),
          equals(false));
      expect(
          tree2.any((NodeId node) => session1.getNodePersistor(node) != null),
          equals(false));

      // Adding an edge from a persisted node to tree1 causes its nodes to be
      // persisted (in ledger1, since that's where sessionRoot1 lives).
      sHelper1.addEdge(session1Root, ['foo'], treeRoot1);
      for (NodeId node in tree1) {
        expect(session1.getNodePersistor(node), same(ledger1));
        expect(ledger1.node(node)?.getValue('depth'), isNotNull);
      }

      // Similarly, adding an edge from a node from session2 causes the tree
      // nodes to be persisted there.  Furthermore, session2 persists these all
      // the way down to ledger2.
      sHelper1.addEdge(session2Root, ['foo'], treeRoot2);
      // for (NodeId node in tree2) {
      // TODO(jjosh): the desired semantics need work.  For example, in this
      // case the tree edge-labels "branch" and "leaf" don't match the link
      // query.  What should happen?  Ideas:
      // - nodes/edges should immediately disappear from the SessionGraph:
      //   they are now "owned" by session2 and are not visible because they
      //   don't match the query.  This might be surprising to the module that
      //   wrote them.
      // - detect this situation, and immediately raise an error.  This has
      //   two issues:
      // A problem common to both these ideas is there is no good way for the
      // writer to check in advance what would happen, for several reasons:
      // - the ultimate writer is probably a module; it seems infeasible for
      //   the .mojom to support these queries, especially since other changes
      //   to the graph may occur before the response is received.
      // - the original writer of the not-yet-persisted nodes/edges may not be
      //   the same as the writer of the final edge that would persist the
      //   others.

      // expect(session1.getNodePersistor(node), same(session2));
      // expect(session2.getNodePersistor(node), same(ledger2));
      // expect(ledger2.node(node)?.getValue('depth'), isNotNull);
      // }
    });

    test('Filter Persisted Representation Values', () {
      // The implementation should (?) filter any representation values whose
      // keys aren't part of the link-query.
    }, skip: 'unsupported');

    test('No Cycles Allowed', () {
      // The implementation should detect cycles in session graph links,
      // but currently does not.
    }, skip: 'unsupported');

    test('Link Root Need Not Already Be Persisted', () {
      // Currently, an assertion fails when using an unpersisted node as
      // the link-root.  It is not clear what the correct fix should be.
      // One option would be to immediately persist the node, but to which
      // underlying graph?

      // This is OK, because the link-root is already persisted to ledger1.
      new MutationHelper(ledger1).addNode(id: 'persisted');
      final Node persistedRoot =
          session1.node(new NodeId.fromString('persisted'));
      expect(persistedRoot, isNotNull);
      session1.addSessionLink(session2.sessionId,
          query: new GraphQuery(['foo']),
          linkOrigin: persistedRoot,
          labels: ['bar']);

      // This is not OK, because the link-root hasn't yet been persisted.
      // (because it has no incoming edges that are already persisted).
      final Node unpersistedRoot =
          new MutationHelper(session1).addNode(id: 'unpersisted');
      session1.addSessionLink(session2.sessionId,
          query: new GraphQuery(['foo']),
          linkOrigin: unpersistedRoot,
          labels: ['bar']);
    }, skip: 'unsupported');
  });
}
