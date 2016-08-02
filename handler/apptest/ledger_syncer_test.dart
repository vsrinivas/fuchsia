// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:async';

import 'package:modular/builtin_types.dart';
import 'package:modular_core/graph/graph.dart';
import 'package:modular_core/graph/mem_graph.dart';
import 'package:modular_services/ledger/ledger.mojom.dart';
import 'package:mojo/application.dart';
import 'package:parser/expression.dart';
import 'package:test/test.dart';

import '../mojo/ledger_syncer.dart';

Future<List<Null>> waitForGraphChange(final Iterable<Graph> graphs) {
  final List<Future<Null>> futures = <Future<Null>>[];
  for (final Graph graph in graphs) {
    final Completer<Null> completer = new Completer<Null>();
    final GraphChangeCallback observer = (GraphEvent event) {
      if (!completer.isCompleted) {
        completer.complete();
      }
    };
    completer.future.then((_) {
      graph.removeObserver(observer);
    });
    graph.addObserver(observer);
    futures.add(completer.future);
  }
  return Future.wait(futures).timeout(new Duration(seconds: 2));
}

void expectGraphEquals(Graph g1, Graph g2) {
  expect(g1.nodes.toSet(), equals(g2.nodes.toSet()));
  expect(g1.edges.toSet(), equals(g2.edges.toSet()));
}

Label newLabel(String label) {
  return new Label.fromUri(Uri.parse(label));
}

void testLedgerSyncer(final Application application) {
  group('LedgerSyncer', () {
    test('Test graph synchronization', () async {
      final LedgerProxy proxy = new LedgerProxy.unbound();
      application.connectToService("https://tq.mojoapps.io/ledger.mojo", proxy);
      final Ledger ledger = proxy;
      final Completer<LedgerStatus> completer = new Completer<LedgerStatus>();
      ledger.authenticate("Anonymous",
          (UserId userId, LedgerStatus status) => completer.complete(status));
      expect(await completer.future, equals(LedgerStatus.ok));

      final MemGraph graph1 = new MemGraph();
      final LedgerSyncer ledgerSyncer1 = new LedgerSyncer(graph1, ledger);
      await ledgerSyncer1.init();

      final MemGraph graph2 = new MemGraph();
      final LedgerSyncer ledgerSyncer2 = new LedgerSyncer(graph2, ledger);
      await ledgerSyncer2.initWithSession(ledgerSyncer1.sessionId);

      expect(ledgerSyncer2.sessionId, equals(ledgerSyncer1.sessionId));
      expectGraphEquals(graph1, graph2);

      Node graph1Node;
      // Adding an edge to graph1.
      Future<List<Null>> update = waitForGraphChange([graph1, graph2]);
      graph1.mutate((GraphMutator mutator) {
        graph1Node = mutator.addNode();
        mutator.addEdge(graph1Node.id, ['label1']);
      });
      await update;
      expectGraphEquals(graph1, graph2);

      // Adding an edge to graph2.
      update = waitForGraphChange([graph1, graph2]);
      Node graph2Node;
      graph2.mutate((GraphMutator mutator) {
        graph2Node = mutator.addNode();
        mutator.addEdge(graph2Node.id, ['label2']);
      });
      await update;
      expectGraphEquals(graph1, graph2);

      // Removing an edge to graph1.
      update = waitForGraphChange([graph1, graph2]);
      graph1.mutate((GraphMutator mutator) {
        mutator.removeEdge(graph1.edges.first.id);
      });
      await update;
      expectGraphEquals(graph1, graph2);

      // Removing a node from graph1.
      update = waitForGraphChange([graph1, graph2]);
      graph1.mutate((GraphMutator mutator) {
        mutator.removeNode(graph1.edges.first.target.id);
      });
      await update;
      expectGraphEquals(graph1, graph2);

      // Adding a representation value to graph1.
      update = waitForGraphChange([graph1, graph2]);
      graph1.mutate((GraphMutator mutator) {
        mutator.setValue(
            graph1Node.id, "label3", BuiltinString.write("value1"));
      });
      await update;
      expectGraphEquals(graph1, graph2);

      // Updating the representation value on graph2.
      update = waitForGraphChange([graph1, graph2]);
      graph2.mutate((GraphMutator mutator) {
        mutator.setValue(
            graph2Node.id, "label3", BuiltinString.write("value2"));
      });
      await update;
      expectGraphEquals(graph1, graph2);

      // Removing the representation value on graph1.
      update = waitForGraphChange([graph1, graph2]);
      graph1.mutate((GraphMutator mutator) {
        mutator.setValue(graph1Node.id, "label3", null);
      });
      await update;

      expectGraphEquals(graph1, graph2);

      await Future.wait([ledgerSyncer1.close(), ledgerSyncer2.close()]);
      await proxy.close();
    });
  });
}
