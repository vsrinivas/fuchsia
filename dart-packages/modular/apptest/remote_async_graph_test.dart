// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:async';
import 'dart:typed_data';

import 'package:modular/graph/mojo/graph_server.dart';
import 'package:modular/graph/mojo/remote_async_graph.dart';
import 'package:modular/modular/graph.mojom.dart' as mojom;
import 'package:modular_core/graph/mem_graph.dart';
import 'package:modular_core/graph/mutation.dart';
import 'package:modular_core/graph/ref.dart';
import 'package:modular_core/graph/test_utils/test_observer.dart';
import 'package:modular_core/log.dart';
import 'package:mojo/core.dart';
import 'package:test/test.dart';

void testRemoteAsyncGraph() {
  MemGraph remoteGraph;
  GraphServer remoteGraphServer;
  mojom.GraphProxy proxy;
  RemoteAsyncGraph graph;
  TestObserver observer;

  setUp(() {
    remoteGraph = new MemGraph();
    final MojoMessagePipe pipe = new MojoMessagePipe();
    remoteGraphServer =
        new GraphServer.fromEndpoint(pipe.endpoints[0], remoteGraph);
    proxy = new mojom.GraphProxy.fromEndpoint(pipe.endpoints[1]);
    proxy.ctrl.errorFuture.catchError((final dynamic error) {
      log('RemoteAsyncGraphTest').info('Error in graph connection: $error');
    });
    observer = new TestObserver();
  });

  tearDown(() {
    assert(remoteGraphServer != null);
    remoteGraphServer.close();
    graph?.close();
  });

  const String kTestLabel1 = 'testLabel1';
  final Uint8List kTestData = new Uint8List.fromList([2, 3]);

  group('RemoteAsyncGraph', () {
    test('InitialValues', () async {
      Node node1;
      Node node2;
      Node node3;
      Edge edge;

      // Set up the graph with some nodes and edges.
      remoteGraph.mutate((final GraphMutator mutator) {
        node1 = mutator.addNode();
        node2 = mutator.addNode();
        node3 = mutator.addNode();
        edge = mutator.addEdge(node1.id, [kTestLabel1], node3.id);
        mutator.setValue(node2.id, kTestLabel1, kTestData);
      });

      graph = new RemoteAsyncGraph(proxy);
      expect(graph.isReady, isFalse);
      await graph.waitUntilReady();
      expect(graph.isReady, isTrue);

      // Test the various getters
      expect(graph.nodes.length, equals(3));
      expect(graph.edges.length, equals(1));
      expect(graph.nodes, equals([node1, node2, node3]));
      expect(graph.edges.first.origin, equals(node1));
      expect(graph.edges.first.target, equals(node3));
      expect(graph.edges.first.labels, equals([kTestLabel1]));
      expect(graph.node(node1.id), equals(node1));
      expect(graph.node(node2.id), equals(node2));
      expect(graph.node(node3.id), equals(node3));
      expect(graph.edge(edge.id), equals(edge));
      expect(graph.node(node2.id).valueKeys, equals([kTestLabel1]));
      expect(graph.node(node2.id).getValue(kTestLabel1), equals(kTestData));
      graph.nodes.forEach((n) => expect(n.isDeleted, isFalse));
    });

    test('ValueUpdates', () async {
      // Start with an empty graph.
      graph = new RemoteAsyncGraph(proxy);
      graph.addObserver(observer);
      expect(graph.isReady, isFalse);
      await graph.waitUntilReady();
      expect(graph.isReady, isTrue);
      expect(graph.nodes.isEmpty, isTrue);
      expect(graph.edges.isEmpty, isTrue);

      // The first mojo observer event shouldn't trigger an update on
      // TestObserver.
      expect(observer.calls, equals(0));
      expect(observer.events.isEmpty, isTrue);

      // Trigger some mutations.
      Node node1, node2;
      Edge edge;
      remoteGraph.mutate((final GraphMutator mutator) {
        node1 = mutator.addNode();
        node2 = mutator.addNode();
        edge = mutator.addEdge(node1.id, [kTestLabel1], node2.id);
        mutator.setValue(node2.id, kTestLabel1, kTestData);
        mutator.removeNode(node2.id);
      });

      await observer.waitForNextUpdate();

      expect(observer.calls, equals(1));
      expect(observer.events.length, equals(1));

      // Verify the received mutations.
      GraphMutationList lastMutations = observer.events[0].mutations;
      expect(lastMutations.length, equals(6));
      expect(lastMutations[0].type, equals(GraphMutationType.addNode));
      expect(lastMutations[0].nodeId, equals(node1.id));

      expect(lastMutations[1].type, equals(GraphMutationType.addNode));
      expect(lastMutations[1].nodeId, equals(node2.id));

      expect(lastMutations[2].type, equals(GraphMutationType.addEdge));
      expect(lastMutations[2].edgeId, equals(edge.id));
      expect(lastMutations[2].originNodeId, equals(node1.id));
      expect(lastMutations[2].targetNodeId, equals(node2.id));
      expect(lastMutations[2].labels, equals([kTestLabel1]));

      expect(lastMutations[3].type, equals(GraphMutationType.setValue));
      expect(lastMutations[3].nodeId, equals(node2.id));
      expect(lastMutations[3].valueKey, equals(kTestLabel1));
      expect(lastMutations[3].newValue, kTestData);

      expect(lastMutations[4].type, equals(GraphMutationType.removeEdge));
      expect(lastMutations[4].edgeId, equals(edge.id));
      expect(lastMutations[4].originNodeId, equals(node1.id));
      expect(lastMutations[4].targetNodeId, equals(node2.id));
      expect(lastMutations[4].labels, equals([kTestLabel1]));

      expect(lastMutations[5].type, equals(GraphMutationType.removeNode));
      expect(lastMutations[5].nodeId, equals(node2.id));

      // Verify the contents of the graph.
      expect(graph.nodes.length, equals(1));
      expect(graph.edges.isEmpty, isTrue);
      expect(graph.nodes, equals([node1]));
    });

    test('ApplyMutations', () async {
      // Start with an empty graph.
      graph = new RemoteAsyncGraph(proxy);
      graph.addObserver(observer);
      expect(graph.isReady, isFalse);
      await graph.waitUntilReady();
      expect(graph.isReady, isTrue);
      expect(graph.nodes.isEmpty, isTrue);
      expect(graph.edges.isEmpty, isTrue);

      // Synchronous mutate is not supported.
      expect(() => graph.mutate(null),
          throwsA(new isInstanceOf<UnsupportedError>()));

      Node node1, node2;
      Edge edge;
      Future<Null> result = graph.mutateAsync((final GraphMutator mutator) {
        node1 = mutator.addNode();
        node2 = mutator.addNode();
        edge = mutator.addEdge(node1.id, [kTestLabel1], node2.id);
      });

      expect(node1 == null || node1.isDeleted, isTrue);
      expect(node2 == null || node2.isDeleted, isTrue);

      await result;

      expect(node1.isDeleted, isFalse);
      expect(node2.isDeleted, isFalse);

      expect(graph.nodes, equals([node1, node2]));
      expect(graph.edges, equals([edge]));
      expect(remoteGraph.nodes, equals([node1, node2]));
      expect(remoteGraph.edges, equals([edge]));

      // Remove [node1]. Its ref should remain valid until a complete round
      // trip.
      result = graph.mutateAsync((final GraphMutator mutator) {
        mutator.removeNode(node1.id);
      });

      expect(node1.isDeleted, isFalse);
      expect(graph.edges.length, equals(1));

      await result;

      expect(node1.isDeleted, isTrue);
      expect(graph.nodes, equals([node2]));
      expect(graph.edges.isEmpty, isTrue);
      expect(remoteGraph.nodes, equals([node2]));
      expect(remoteGraph.edges.isEmpty, isTrue);

      // Remove [node2].
      result = graph.mutateAsync((final GraphMutator mutator) {
        mutator.removeNode(node2.id);
      });

      // Remove [node2] again. This one will be processed as a valid mutation on
      // our end but the remote should reject it since [node2] won't exist.
      // Also try to add a new node. This should not apply, since the entire
      // operation fails.
      node1 = null;
      Future<Null> errorResult =
          graph.mutateAsync((final GraphMutator mutator) {
        node1 = mutator.addNode();
        mutator.removeNode(node2.id);
      });

      expect(node1 == null || node1.isDeleted, isTrue);
      expect(errorResult, throwsA(new isInstanceOf<FailedGraphMutation>()));

      try {
        await errorResult;
        fail('Future should have resulted in an error');
      } catch (e) {}

      expect(node1.isDeleted, isTrue); // [node1] never applied
      expect(node2.isDeleted, isTrue); // [node2] removed by first call above
      expect(graph.nodes.isEmpty, isTrue);
      expect(graph.edges.isEmpty, isTrue);
    });
  });
}
