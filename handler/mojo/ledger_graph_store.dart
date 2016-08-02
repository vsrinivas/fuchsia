// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:async';

import 'package:modular_core/uuid.dart';
import 'package:modular_core/graph/async_graph.dart';
import 'package:modular_core/graph/graph.dart';
import 'package:modular_core/graph/id.dart';
import 'package:handler/constants.dart';
import 'package:handler/graph/graph_store.dart';
import 'package:modular_core/util/timeline_helper.dart';
import 'package:modular_services/ledger2/ledger2.mojom.dart' as mojom;

import 'ledger_graph.dart';

import 'errors.dart';

/// [LedgerGraphStore] is an implementation of GraphStore that returns instances
/// of [AsyncGraph] that are hooked up to the ledger.
class LedgerGraphStore implements GraphStore {
  final Map<Uuid, Future<AsyncGraph>> _graphs = <Uuid, Future<AsyncGraph>>{};

  final mojom.Ledger _ledger;

  /// TODO(jjosh): Handle authentication.
  LedgerGraphStore(this._ledger);

  @override
  Future<Graph> createGraph(Uuid sessionId) {
    assert(sessionId != null);
    if (_graphs.containsKey(sessionId)) {
      throw new GraphAlreadyExists(sessionId, _graphs[sessionId]);
    }

    final completer = new Completer<AsyncGraph>();
    _graphs[sessionId] = completer.future;

    traceAsync('$runtimeType createGraph', () async {
      final mojom.LedgerMutatorProxy mutatorProxy =
          new mojom.LedgerMutatorProxy.unbound();

      final mojom.LedgerObserverStub observerStub =
          new mojom.LedgerObserverStub.unbound();

      final Completer<mojom.LedgerStatus> statusCompleter =
          new Completer<mojom.LedgerStatus>();
      _ledger.createSession(
          sessionId.toUint8List(),
          mutatorProxy,
          observerStub,
          (final mojom.LedgerStatus status) =>
              statusCompleter.complete(status));
      final mojom.LedgerStatus result = await statusCompleter.future;

      if (result == mojom.LedgerStatus.ok) {
        // Success!  We now have a connection to a new ledger graph.  Populate
        // the initial metadata.
        final AsyncGraph graph = new LedgerGraph(mutatorProxy, observerStub);
        await graph.mutateAsync((GraphMutator mutator) {
          // Since we want to control the ID of the root node directly, we have
          // to apply a low-level mutation.
          final NodeId rootId = new NodeId.fromString(
              '${Constants.sessionGraphLabelPrefix}rootNode:$sessionId');
          mutator.apply(new GraphMutation.addNode(rootId));

          Node metadataNode = mutator.addNode();
          mutator.setValue(metadataNode.id, Constants.sessionGraphIdLabel,
              sessionId.toUint8List());
          mutator.addEdge(rootId, [Constants.metadataLabel], metadataNode.id);
        });

        completer.complete(graph);
      } else {
        // We failed for a possibly-transient reason.  Clear the value so that
        // subsequent calls to createGraph() and/or findGraph() have a chance
        // to succeed.
        _graphs.remove(sessionId);

        if (result == mojom.LedgerStatus.sessionAlreadyExists) {
          completer.completeError(
              new GraphAlreadyExists(sessionId, findGraph(sessionId)));
        } else if (result == mojom.LedgerStatus.authenticationError) {
          completer.completeError(new AuthenticationException(
              'ledger disallowed access to session $sessionId'));
        } else {
          String reason = (result == mojom.LedgerStatus.internalError)
              ? 'internal error'
              : 'unknown';
          completer.completeError(new GraphUnavailable(sessionId, reason));
        }
      }
    });
    return completer.future;
  }

  @override
  Future<Graph> findGraph(Uuid sessionId) {
    assert(sessionId != null);
    if (_graphs.containsKey(sessionId)) {
      // TODO(jjosh): wrap the returned future to catch potential
      // GraphAlreadyExists errors... these shouldn't be allowed to bother
      // callers of findGraph().
      return _graphs[sessionId];
    }

    final completer = new Completer<AsyncGraph>();
    _graphs[sessionId] = completer.future;

    traceAsync('$runtimeType findGraph', () async {
      final mojom.LedgerMutatorProxy mutatorProxy =
          new mojom.LedgerMutatorProxy.unbound();

      final mojom.LedgerObserverStub observerStub =
          new mojom.LedgerObserverStub.unbound();

      final Completer<mojom.LedgerStatus> completer =
          new Completer<mojom.LedgerStatus>();
      _ledger.connectToSession(
          sessionId.toUint8List(),
          mutatorProxy,
          observerStub,
          (final mojom.LedgerStatus status) => completer.complete(status));
      final mojom.LedgerStatus result = await completer.future;

      if (result == mojom.LedgerStatus.ok) {
        completer.complete(new LedgerGraph(mutatorProxy, observerStub));
      } else {
        // We failed for a possibly-transient reason.  Clear the value so that
        // subsequent calls to createGraph() and/or findGraph() have a chance
        // to succeed.
        _graphs.remove(sessionId);

        if (result == mojom.LedgerStatus.sessionDoesNotExist) {
          completer.completeError(
              new GraphAlreadyExists(sessionId, findGraph(sessionId)));
        } else if (result == mojom.LedgerStatus.authenticationError) {
          completer.completeError(new AuthenticationException(
              'ledger disallowed access to session $sessionId'));
        } else {
          String reason = (result == mojom.LedgerStatus.internalError)
              ? 'internal error'
              : 'unknown';
          completer.completeError(new GraphUnavailable(sessionId, reason));
        }
      }
    });

    return completer.future;
  }
}
