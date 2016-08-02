// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:async';
import 'dart:core';
import 'dart:typed_data';

import 'package:modular/graph/mojo/graph_server.dart';
import 'package:modular/modular/graph.mojom.dart' as mojom;
import 'package:modular_core/graph/id.dart';
import 'package:modular_core/graph/mem_graph.dart';
import 'package:modular_core/graph/mutation.dart';
import 'package:modular_core/graph/ref.dart';
import 'package:modular_core/log.dart';
import 'package:mojo/core.dart';
import 'package:test/test.dart';

class TestGraphObserver implements mojom.GraphObserver {
  int onChangeCount = 0;
  List<mojom.GraphMutation> lastMutations;

  mojom.GraphObserverStub _stub;
  final List<Completer> _onChangeCompleters = [];

  TestGraphObserver(final mojom.GraphProxy graphProxy) {
    assert(graphProxy != null);
    _stub = new mojom.GraphObserverStub.unbound()..impl = this;
    graphProxy.addObserver(_stub);
  }

  /// Closes the connection to the underlying endpoint. Call this to avoid
  /// leaking mojo handles.
  void close() {
    assert(_stub != null);
    _stub.close();
  }

  Future<Null> waitForNextOnChange() {
    Completer completer = new Completer();
    _onChangeCompleters.add(completer);
    final Duration timeout = new Duration(seconds: 5);
    return completer.future.timeout(timeout,
        onTimeout: () => throw new TimeoutException(
            'Timed out while waiting for GraphObserver.onChange', timeout));
  }

  @override // mojom.GraphObserver
  void onChange(List<mojom.GraphMutation> mutations, void callback()) {
    onChangeCount++;
    this.lastMutations = mutations;

    _onChangeCompleters.forEach((c) => c.complete());
    _onChangeCompleters.clear();

    callback();
  }
}

void testGraphServer() {
  MemGraph graph;
  GraphServer graphServer;
  mojom.GraphProxy proxy;
  TestGraphObserver observer;

  setUp(() {
    graph = new MemGraph();
    final MojoMessagePipe pipe = new MojoMessagePipe();
    graphServer = new GraphServer.fromEndpoint(pipe.endpoints[0], graph);
    proxy = new mojom.GraphProxy.fromEndpoint(pipe.endpoints[1]);
    proxy.ctrl.errorFuture.catchError((final dynamic error) {
      log('GraphServerTest').info('Error in graph connection: $error');
    });
  });

  tearDown(() {
    assert(graphServer != null);
    graphServer.close();
    observer?.close();
  });

  const String kTestLabel1 = 'testLabel1';
  const String kTestLabel2 = 'testLabel2';
  final Uint8List kTestData = new Uint8List.fromList([2, 3]);

  group('GraphServer', () {
    test('InitialValues', () async {
      Node node1;
      Node node2;
      Node node3;
      Edge edge;

      // Set up the graph with some nodes and edges.
      graph.mutate((final GraphMutator mutator) {
        node1 = mutator.addNode();
        node2 = mutator.addNode();
        node3 = mutator.addNode();
        edge = mutator.addEdge(node1.id, [kTestLabel1], node3.id);
        mutator.setValue(node2.id, kTestLabel1, kTestData);
      });

      observer = new TestGraphObserver(proxy);
      await observer.waitForNextOnChange();

      expect(observer.onChangeCount, equals(1));
      expect(observer.lastMutations, isNotNull);

      // We should have received 5 mutations for the 3 nodes, 1 edge, and 1
      // value modification. The initial notification contains all nodes and
      // their values, followed by edges, in the order they were added to the
      // graph (since we're using a MemGraph here).
      expect(observer.lastMutations.length, equals(5));
      expect(observer.lastMutations[0].nodeAdded.nodeId,
          equals(node1.id.toString()));
      expect(observer.lastMutations[1].nodeAdded.nodeId,
          equals(node2.id.toString()));

      expect(observer.lastMutations[2].valueChanged.nodeId,
          equals(node2.id.toString()));
      expect(observer.lastMutations[2].valueChanged.key, equals(kTestLabel1));
      expect(
          observer.lastMutations[2].valueChanged.newValue, equals(kTestData));

      expect(observer.lastMutations[3].nodeAdded.nodeId,
          equals(node3.id.toString()));

      expect(observer.lastMutations[4].edgeAdded.edgeId,
          equals(edge.id.toString()));
      expect(observer.lastMutations[4].edgeAdded.originNodeId,
          equals(node1.id.toString()));
      expect(observer.lastMutations[4].edgeAdded.targetNodeId,
          equals(node3.id.toString()));
      expect(observer.lastMutations[4].edgeAdded.labels, equals([kTestLabel1]));
    });

    test('ValueUpdates', () async {
      // We start with an empty graph.
      observer = new TestGraphObserver(proxy);
      await observer.waitForNextOnChange();

      expect(observer.onChangeCount, equals(1));
      expect(observer.lastMutations, isNotNull);
      expect(observer.lastMutations.isEmpty, isTrue);

      // Each atomic mutate operation should trigger a single OnChange event.
      // The mutations should be in the same order they were applied to the
      // graph.
      Node node1, node2;
      graph.mutate((final GraphMutator mutator) {
        node1 = mutator.addNode();
        node2 = mutator.addNode();
      });

      await observer.waitForNextOnChange();
      expect(observer.onChangeCount, equals(2));
      expect(observer.lastMutations, isNotNull);
      expect(observer.lastMutations.length, equals(2));
      expect(observer.lastMutations[0].nodeAdded.nodeId,
          equals(node1.id.toString()));
      expect(observer.lastMutations[1].nodeAdded.nodeId,
          equals(node2.id.toString()));

      Node node3;
      Edge edge12, edge23;
      graph.mutate((final GraphMutator mutator) {
        node3 = mutator.addNode();
        edge12 = mutator.addEdge(node1.id, [kTestLabel1], node2.id);
        edge23 = mutator.addEdge(node2.id, [kTestLabel1], node3.id);
      });

      await observer.waitForNextOnChange();
      expect(observer.onChangeCount, equals(3));
      expect(observer.lastMutations, isNotNull);
      expect(observer.lastMutations.length, equals(3));
      expect(observer.lastMutations[0].nodeAdded.nodeId,
          equals(node3.id.toString()));
      expect(observer.lastMutations[1].edgeAdded.edgeId,
          equals(edge12.id.toString()));
      expect(observer.lastMutations[1].edgeAdded.originNodeId,
          equals(node1.id.toString()));
      expect(observer.lastMutations[1].edgeAdded.targetNodeId,
          equals(node2.id.toString()));
      expect(observer.lastMutations[2].edgeAdded.edgeId,
          equals(edge23.id.toString()));
      expect(observer.lastMutations[2].edgeAdded.originNodeId,
          equals(node2.id.toString()));
      expect(observer.lastMutations[2].edgeAdded.targetNodeId,
          equals(node3.id.toString()));

      // Removing node2 should remove its edges as well.
      graph.mutate((final GraphMutator mutator) {
        mutator.removeNode(node2.id);
      });

      await observer.waitForNextOnChange();
      expect(observer.onChangeCount, equals(4));
      expect(observer.lastMutations, isNotNull);
      expect(observer.lastMutations.length, equals(3));
      expect(observer.lastMutations[0].edgeRemoved.edgeId,
          equals(edge12.id.toString()));
      expect(observer.lastMutations[1].edgeRemoved.edgeId,
          equals(edge23.id.toString()));
      expect(observer.lastMutations[2].nodeRemoved.nodeId,
          equals(node2.id.toString()));
    });

    test('ApplyMutations', () async {
      // Set up our observer with an empty graph.
      observer = new TestGraphObserver(proxy);
      await observer.waitForNextOnChange();
      expect(observer.onChangeCount, equals(1));
      expect(observer.lastMutations, isNotNull);
      expect(observer.lastMutations.isEmpty, isTrue);
      expect(graph.nodes.isEmpty, isTrue);
      expect(graph.edges.isEmpty, isTrue);

      // Add some nodes.
      final PrefixNodeIdGenerator nodeIdGen = new PrefixNodeIdGenerator(null);
      final PrefixEdgeIdGenerator edgeIdGen = new PrefixEdgeIdGenerator(null);
      NodeId nodeId1 = nodeIdGen();
      NodeId nodeId2 = nodeIdGen();
      NodeId nodeId3 = nodeIdGen();

      final List<mojom.GraphMutation> mutations = <mojom.GraphMutation>[];
      mutations.add(new mojom.GraphMutation()
        ..nodeAdded = (new mojom.NodeMutation()..nodeId = '$nodeId1'));
      mutations.add(new mojom.GraphMutation()
        ..nodeAdded = (new mojom.NodeMutation()..nodeId = '$nodeId2'));
      mutations.add(new mojom.GraphMutation()
        ..nodeAdded = (new mojom.NodeMutation()..nodeId = '$nodeId3'));

      Completer<mojom.GraphStatus> completer = new Completer();
      proxy.applyMutations(
          mutations,
          (final mojom.GraphStatus status, final String errorDescription) =>
              completer.complete(status));
      expect(await completer.future, equals(mojom.GraphStatus.success));
      if (observer.onChangeCount == 1) await observer.waitForNextOnChange();
      expect(observer.onChangeCount, equals(2));
      expect(observer.lastMutations.length, equals(3));
      expect(graph.nodes.length, equals(3));
      expect(graph.edges.isEmpty, isTrue);
      expect(graph.nodes.map((n) => n.id).toList(),
          equals([nodeId1, nodeId2, nodeId3]));

      // Add edges and write a value.
      EdgeId edgeId12 = edgeIdGen();
      EdgeId edgeId23 = edgeIdGen();
      EdgeId edgeId13 = edgeIdGen();

      mutations.clear();
      mutations.add(new mojom.GraphMutation()
        ..edgeAdded = (new mojom.EdgeMutation()
          ..edgeId = '$edgeId12'
          ..originNodeId = '$nodeId1'
          ..targetNodeId = '$nodeId2'
          ..labels = [kTestLabel1]));
      mutations.add(new mojom.GraphMutation()
        ..edgeAdded = (new mojom.EdgeMutation()
          ..edgeId = '$edgeId23'
          ..originNodeId = '$nodeId2'
          ..targetNodeId = '$nodeId3'
          ..labels = [kTestLabel2]));
      mutations.add(new mojom.GraphMutation()
        ..edgeAdded = (new mojom.EdgeMutation()
          ..edgeId = '$edgeId13'
          ..originNodeId = '$nodeId1'
          ..targetNodeId = '$nodeId3'
          ..labels = [kTestLabel1, kTestLabel2]));
      mutations.add(new mojom.GraphMutation()
        ..valueChanged = (new mojom.NodeValueMutation()
          ..nodeId = '$nodeId2'
          ..key = kTestLabel1
          ..newValue = kTestData));

      completer = new Completer();
      proxy.applyMutations(
          mutations,
          (final mojom.GraphStatus status, final String errorDescription) =>
              completer.complete(status));
      expect(await completer.future, equals(mojom.GraphStatus.success));
      if (observer.onChangeCount == 2) await observer.waitForNextOnChange();
      expect(observer.onChangeCount, equals(3));
      expect(observer.lastMutations.length, equals(4));
      expect(graph.nodes.length, equals(3));
      expect(graph.edges.length, equals(3));

      expect(graph.edges.map((e) => e.id).toList(),
          equals([edgeId12, edgeId23, edgeId13]));
      expect(graph.edge(edgeId12).origin.id, equals(nodeId1));
      expect(graph.edge(edgeId12).target.id, equals(nodeId2));
      expect(graph.edge(edgeId12).labels, equals([kTestLabel1]));

      expect(graph.edge(edgeId23).origin.id, equals(nodeId2));
      expect(graph.edge(edgeId23).target.id, equals(nodeId3));
      expect(graph.edge(edgeId23).labels, equals([kTestLabel2]));

      expect(graph.edge(edgeId13).origin.id, equals(nodeId1));
      expect(graph.edge(edgeId13).target.id, equals(nodeId3));
      expect(graph.edge(edgeId13).labels, equals([kTestLabel1, kTestLabel2]));

      expect(graph.node(nodeId2).getValue(kTestLabel1), equals(kTestData));

      // Remove a node that has edges that reference it. This should fail. The
      // underlying MemGraph will throw an exception which will result in a
      // response message that contains a failure status.
      mutations.clear();
      mutations.add(new mojom.GraphMutation()
        ..nodeRemoved = (new mojom.NodeMutation()..nodeId = '$nodeId1'));
      completer = new Completer();
      proxy.applyMutations(
          mutations,
          (final mojom.GraphStatus status, final String errorDescription) =>
              completer.complete(status));
      expect(await completer.future, equals(mojom.GraphStatus.failure));
      expect(observer.onChangeCount, equals(3));
      expect(graph.nodes.length, equals(3));

      // Removing the referencing edges before removing the node should work.
      mutations.insert(
          0,
          new mojom.GraphMutation()
            ..edgeRemoved = (new mojom.EdgeMutation()
              ..edgeId = '$edgeId12'
              ..originNodeId = '$nodeId1'
              ..targetNodeId = '$nodeId2'
              ..labels = [kTestLabel1]));
      mutations.insert(
          0,
          new mojom.GraphMutation()
            ..edgeRemoved = (new mojom.EdgeMutation()
              ..edgeId = '$edgeId13'
              ..originNodeId = '$nodeId1'
              ..targetNodeId = '$nodeId3'
              ..labels = [kTestLabel1, kTestLabel2]));

      completer = new Completer();
      proxy.applyMutations(
          mutations,
          (final mojom.GraphStatus status, final String errorDescription) =>
              completer.complete(status));
      expect(await completer.future, equals(mojom.GraphStatus.success));
      if (observer.onChangeCount == 3) await observer.waitForNextOnChange();
      expect(observer.onChangeCount, equals(4));
      expect(observer.lastMutations.length, equals(3));
      expect(graph.nodes.length, equals(2));
      expect(graph.edges.length, equals(1));
    });
  });
}
