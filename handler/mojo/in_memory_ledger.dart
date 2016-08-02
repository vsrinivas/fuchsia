// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:modular_services/ledger2/ledger2.mojom.dart';

import 'package:modular_core/graph/graph.dart';
import 'package:modular_core/graph/mem_graph_state.dart';
import 'package:modular/graph/mojo/mutation_utils.dart';
import 'package:modular_core/graph/validating_lazy_cloned_graph_state.dart';
import 'package:modular_core/log.dart';
import 'package:modular/modular/graph.mojom.dart' as mojom;
import 'package:modular_core/uuid.dart';

final Logger _log = log('InMemoryLedger');

// This is a simple in-memory version of the same semantics implemented by
// Ledger and FirebaseGraph.  The implementation is greatly simplified
// by being able to synchronously apply mutations, rather than using Firebase
// transactions.
class InMemoryLedger implements Ledger {
  final Map<Uuid, _Graph> _graphs = <Uuid, _Graph>{};

  @override
  void createSession(List<int> uuid, Object mutator, Object observer,
      void callback(LedgerStatus status)) {
    LedgerMutatorStub mutatorStub = mutator as LedgerMutatorStub;
    LedgerObserverProxy observerProxy = observer as LedgerObserverProxy;

    final sessionId = new Uuid(uuid);

    // Fail if graph already exists.
    if (_graphs.containsKey(uuid)) {
      mutatorStub.close();
      observerProxy.close();
      callback(LedgerStatus.sessionAlreadyExists);
      return;
    }

    // Create new graph.
    final _Graph graph = new _Graph();
    _graphs[sessionId] = graph;

    // Add connection to graph.
    final _Connection connection =
        new _Connection(graph, mutatorStub, observerProxy);
    mutatorStub.impl = connection;
    graph.connections.add(connection);

    callback(LedgerStatus.ok);
  }

  @override
  void connectToSession(List<int> uuid, Object mutator, Object observer,
      void callback(LedgerStatus status)) {
    LedgerMutatorStub mutatorStub = mutator as LedgerMutatorStub;
    LedgerObserverProxy observerProxy = observer as LedgerObserverProxy;

    final sessionId = new Uuid(uuid);

    // Fail if graph doesn't already exist.
    final _Graph graph = _graphs[sessionId];
    if (graph == null) {
      mutatorStub.close();
      observerProxy.close();
      callback(LedgerStatus.sessionDoesNotExist);
      return;
    }

    // Add connection to graph.
    final _Connection connection =
        new _Connection(graph, mutatorStub, observerProxy);
    mutatorStub.impl = connection;
    graph.connections.add(connection);

    // Notify observer of entire event history.
    graph.history.forEach(connection.observer.onChange);

    callback(LedgerStatus.ok);
  }
}

class _Connection implements LedgerMutator {
  final _Graph graph;
  final LedgerMutatorStub stub;
  final LedgerObserverProxy proxy;
  final LedgerObserver observer;

  _Connection(this.graph, this.stub, proxy)
      : proxy = proxy,
        observer = proxy {
    // TODO(jjosh): remove connection from graph when mutator and/or observer
    // are closed.
  }

  @override
  void applyEvent(mojom.GraphEvent event) {
    try {
      final Iterable<GraphMutation> mutations =
          event.mutations.map(mojomMutationToDart);
      final validator = new ValidatingLazyClonedGraphState(graph.state);

      // Validate mutations before applying them.
      List<GraphMutation> validatedMutations;
      try {
        // The iteratable returned by where() is lazy, so if we don't call
        // toList(), any failed mutations will throw later, outside the try{}.
        validatedMutations = mutations.where(validator.applyMutation).toList();
      } on FailedGraphMutation catch (ex) {
        _log.warning('failed to validate mutation: $ex');
        observer.onFailure(event, dartMutationToMojom(ex.failedMutation));
        return;
      }

      // Apply validated mutations.
      validatedMutations.forEach(graph.state.applyMutation);

      // Adjust event before notifying observers, since validation may have
      // removed redundant mutations.
      event.mutations = validatedMutations.map(dartMutationToMojom).toList();

      // Notify ourself first, regardless of whether the event's mutations
      // were completely redundant.
      observer.onSuccess(event);

      // Only store the event in the history if it has mutations.
      if (event.mutations.isNotEmpty) {
        graph.history.add(event);
        graph.connections.forEach((_Connection c) {
          if (!identical(this, c)) c.observer.onChange(event);
        });
      }
    } catch (unexpected) {
      // The only expected failures are during mutation validation.
      _log.severe('unexpected internal error: $unexpected');
      observer.onFailure(event, event.mutations.first);
      // TODO(jjosh): nuke graph state and replay all previous successful
      // events to ensure that this failure doesn't permanently bork anything.
    }
  }
}

class _Graph {
  final MemGraphState state = new MemGraphState();
  final List<mojom.GraphEvent> history = <mojom.GraphEvent>[];
  final Set<_Connection> connections = new Set<_Connection>();
}
