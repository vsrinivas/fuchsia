// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:async';
import 'dart:core';

import 'package:modular_core/uuid.dart';
import 'package:modular_services/ledger/ledger.mojom.dart';
import 'package:modular_services/synced_store/synced_store.mojom.dart';
import 'package:mojo/core.dart';

import 'json_query_builder.dart';
import 'row_serialization.dart';

/// The implementation of the Ledger interface.
///
/// The current schema for storing sessions maintains a single table holding
/// information on:
/// 1) The session graph nodes and edges (key: session id, node/edge id):
///     session_<base64-sessionId>/node_<base64-nodeId>
///     : <node contents in json>
///     session_<base64-sessionId>/edge_<base64-edgeId>
///     : <edge contents in json>
/// 2) The representation values of nodes (key: session id, corresponding node
///    id, base64 representation of the label):
///     session_<base64-sessionId>/representation_<base64-nodeId>/<base64-label>
///     : {"label": <label>, "value": <value>}
/// 3) The steps of the recipe (key: session id, step id):
///     session_<base64-sessionId>/step_<base64-stepId>
///     : <step contents in json>
///
/// Once a row is created, it is never removed from the synced store. If
/// necessary, it is instead marked as deleted: for nodes and edges the value is
/// updated so that the [deleted] field of their json representation is set to
/// [true] and for representation values, the [value] field is set to [null].
///
/// [SessionId]s, used in the keys of all rows of the key-value store, are
/// randomly generated and returned by the [createSession] method. Node, edge
/// and step ids, also required for storing the row keys, are extracted from the
/// arguments of the [updateSessionGraph] and [addSteps] methods.
class LedgerImpl implements Ledger {
  // TODO(nellyv): Uniquely identify current device instead of randomly creating
  // an at Ledger's initialization.
  /// The current device's id.
  final DeviceId _deviceId;

  /// The current user's id.
  UserId _userId;

  final SyncedStore _syncedStore;
  final List<LedgerObserverProxy> _observerProxies = [];

  LedgerImpl(this._syncedStore)
      : _deviceId = new DeviceId()..id = new Uuid.random().toBase64();

  Future<Null> close({bool immediate: false}) async {
    // Create a copy of the observers list before using it to avoid concurrent
    // modifications.
    for (final LedgerObserverProxy observer
        in new List<LedgerObserverProxy>.from(_observerProxies)) {
      await observer.close(immediate: immediate);
    }
  }

  @override
  void authenticate(
      String username, void callback(UserId userId, LedgerStatus status)) {
    _syncedStore.authenticate(username,
        (final AuthData authData, final SyncedStoreStatus status) {
      if (status == SyncedStoreStatus.authenticationError) {
        // Authentication failed.
        callback(null, LedgerStatus.notAuthorized);
      } else {
        // TODO(nellyv): implement authentication and replace this with the actual
        // UserId information.
        _userId = new UserId()..id = new Uuid.random().toBase64();
        callback(_userId, LedgerStatus.ok);
      }
    });
  }

  // TODO(nellyv): Implement ACLs.
  bool _isAuthorized() => _userId != null;

  @override
  void createSession(void callback(SessionId sessionId, LedgerStatus status)) {
    if (!_isAuthorized()) {
      callback(null, LedgerStatus.notAuthorized);
    } else {
      callback(
          new SessionId()..id = new Uuid.random().toBase64(), LedgerStatus.ok);
    }
  }

  @override
  Future<Null> getSessionGraph(SessionId sessionId, LedgerOptions options,
      void callback(SessionGraph sessionGraph, LedgerStatus status)) async {
    if (!_isAuthorized()) {
      callback(null, LedgerStatus.notAuthorized);
      return;
    }
    final SessionGraph graph = _newSessionGraph();

    // Load all nodes and edges.
    if (!(await _tryLoad(graph, SessionRow.nodeRow(sessionId))) ||
        !(await _tryLoad(graph, SessionRow.edgeRow(sessionId))) ||
        !(await _tryLoad(graph, SessionRow.representationRow(sessionId),
            checkNotDeleted: false))) {
      callback(null, LedgerStatus.internalError);
    } else {
      callback(graph, LedgerStatus.ok);
    }
  }

  /// Uses the key of the [queryRow] to query the rows of the [SyncedStore] and
  /// loads the results (node or edges) on the given [SessionGraph]. Returns
  /// true if the session graph was loaded successfully; false otherwise.
  Future<bool> _tryLoad(SessionGraph graph, SessionRow queryRow,
      {bool checkNotDeleted: true}) {
    final Completer<bool> completer = new Completer<bool>();
    void callback(
        final Map<String, String> keyValues, final SyncedStoreStatus status) {
      if (status == SyncedStoreStatus.internalError) {
        // Querying the SyncedStore failed.
        completer.complete(false);
      } else {
        _rowsToSessionGraph(keyValues, graph);
        completer.complete(true);
      }
    }

    if (checkNotDeleted) {
      _syncedStore.getByValueAttributes(
          queryRow.printableKey(),
          (new JsonQueryBuilder()..setExpectedDeleted(deleted: false))
              .buildQuery(),
          callback);
    } else {
      _syncedStore.getByPrefix([queryRow.printableKey()], callback);
    }
    return completer.future;
  }

  /// Adds the given node and edge rows to the given session graph.
  static void _rowsToSessionGraph(
      Map<String, String> rows, SessionGraph graph) {
    rows.forEach((String key, String value) {
      final SessionRow row = SessionRow.fromSyncedStore(key, value);
      row.addInGraph(graph);
    });
  }

  @override
  void addObserver(SessionId sessionId, PathExpression query,
      LedgerObserverInterface observer, void callback(LedgerStatus status)) {
    if (!_isAuthorized()) {
      callback(LedgerStatus.notAuthorized);
      return;
    }
    if (query != null) {
      throw new Exception("Not implemented, yet");
    }
    final _SyncedStoreObserver syncedStoreObserver =
        new _SyncedStoreObserver(observer, query);
    final SessionRow prefixRow = SessionRow.sessionPrefix(sessionId);
    _syncedStore.addObserver(prefixRow.printableKey(), null,
        syncedStoreObserver.stub, (final SyncedStoreStatus _) {});
    _observerProxies.add(observer);
    observer.ctrl.onError = (MojoEventHandlerError e) {
      observer.close();
      _observerProxies.remove(observer);
      syncedStoreObserver.stub.close();
    };
    callback(LedgerStatus.ok);
  }

  @override
  Future<Null> updateSessionGraph(
      SessionId sessionId,
      List<NodeRecord> nodesToAdd,
      List<EdgeRecord> edgesToAdd,
      List<Representation> valuesToUpdate,
      List<NodeId> nodesToRemove,
      List<EdgeId> edgesToRemove,
      void callback(LedgerStatus status)) async {
    if (!_isAuthorized()) {
      callback(LedgerStatus.notAuthorized);
      return;
    }
    // A growable copy of value updates where we can add representation values
    // of new nodes.
    final List<Representation> valuesToUpdateCopy = <Representation>[]
      ..addAll(valuesToUpdate ?? <Representation>[]);

    // Put all nodes and edges to add or to delete in a single map and then
    // update the syncedStore in a single transaction.
    final Map<String, String> rows = <String, String>{};
    for (NodeRecord node in nodesToAdd ?? <NodeRecord>[]) {
      if (node.creationDevice != null || node.creationUser != null) {
        callback(LedgerStatus.illegalArgument);
        return;
      }
      node.creationDevice = _deviceId;
      node.creationUser = _userId;
      valuesToUpdateCopy
          .addAll(node.representationValues ?? <Representation>[]);

      final SessionRow row = SessionRow.nodeRow(sessionId, node.nodeId, node);
      rows[row.printableKey()] = row.printableValue();
    }

    for (EdgeRecord edge in edgesToAdd ?? const <EdgeRecord>[]) {
      if (edge.creationDevice != null || edge.creationUser != null) {
        callback(LedgerStatus.illegalArgument);
        return;
      }
      edge.creationDevice = _deviceId;
      edge.creationUser = _userId;

      final SessionRow row = SessionRow.edgeRow(sessionId, edge.edgeId, edge);
      rows[row.printableKey()] = row.printableValue();
    }

    for (Representation value in valuesToUpdateCopy) {
      final SessionRow row = SessionRow.representationRow(
          sessionId, value.nodeId, value.label, value);
      rows[row.printableKey()] = row.printableValue();
    }

    for (EdgeId edgeId in edgesToRemove ?? const <EdgeId>[]) {
      final EdgeRow row = await _getEdgeRow(sessionId, edgeId);
      if (row == null) {
        callback(LedgerStatus.illegalArgument);
        return;
      }
      row.edgeRecord.deleted = true;
      rows[row.printableKey()] = row.printableValue();
    }

    for (NodeId nodeId in nodesToRemove ?? const <NodeId>[]) {
      final NodeRow row = await _getNodeRow(sessionId, nodeId);
      if (row == null) {
        callback(LedgerStatus.illegalArgument);
        return;
      }
      row.nodeRecord.deleted = true;
      rows[row.printableKey()] = row.printableValue();

      // Also remove all edges connected to this node.
      final List<EdgeRow> edgeRows =
          await _connectedEdgeRows(sessionId, nodeId);
      if (edgeRows == null) {
        callback(LedgerStatus.internalError);
        return;
      }
      for (EdgeRow edgeRow in edgeRows) {
        edgeRow.edgeRecord.deleted = true;
        rows[edgeRow.printableKey()] = edgeRow.printableValue();
      }
    }
    final SyncedStoreStatus responseStatus = await _putRows(rows);
    if (responseStatus != SyncedStoreStatus.ok) {
      callback(LedgerStatus.internalError);
    } else {
      callback(LedgerStatus.ok);
    }
  }

  /// Returns the rows corresponding to the edges connected to the given node,
  /// or null in case of internal error. It is not an error if there are no
  /// such edges (in which case an empty list is returned).
  Future<List<EdgeRow>> _connectedEdgeRows(
      SessionId sessionId, NodeId nodeId) async {
    final Completer<List<EdgeRow>> completer = new Completer<List<EdgeRow>>();
    final String edgePrefix = SessionRow.edgeRow(sessionId).printableKey();

    _syncedStore.getByValueAttributes(edgePrefix,
        (new JsonQueryBuilder()..setExpectedEnd(nodeId)).buildQuery(),
        (final Map<String, String> incomingKeyValues,
            final SyncedStoreStatus incomingStatus) {
      if (incomingStatus != SyncedStoreStatus.ok) {
        completer.complete(null);
        return;
      }
      _syncedStore.getByValueAttributes(edgePrefix,
          (new JsonQueryBuilder()..setExpectedStart(nodeId)).buildQuery(),
          (final Map<String, String> outgoingKeyValues,
              final SyncedStoreStatus outgoingStatus) {
        if (outgoingStatus != SyncedStoreStatus.ok) {
          completer.complete(null);
          return;
        }
        final List<EdgeRow> result = <EdgeRow>[];
        incomingKeyValues.forEach((String key, String value) {
          result.add(SessionRow.fromSyncedStore(key, value));
        });
        outgoingKeyValues.forEach((String key, String value) {
          result.add(SessionRow.fromSyncedStore(key, value));
        });
        completer.complete(result);
      });
    });

    return completer.future;
  }

  Future<EdgeRow> _getEdgeRow(SessionId sessionId, EdgeId edgeId) async {
    return (await _getRow(SessionRow.edgeRow(sessionId, edgeId))) as EdgeRow;
  }

  Future<NodeRow> _getNodeRow(SessionId sessionId, NodeId nodeId) async {
    return (await _getRow(SessionRow.nodeRow(sessionId, nodeId))) as NodeRow;
  }

  Future<SessionRow> _getRow(SessionRow queryRow) {
    final String key = queryRow.printableKey();
    final Completer<SessionRow> completer = new Completer<SessionRow>();
    _syncedStore.get([key],
        (final Map<String, String> keyValues, final SyncedStoreStatus status) {
      if (status == SyncedStoreStatus.internalError) {
        completer.complete(null);
      } else {
        final String value = keyValues[key];
        completer.complete(SessionRow.fromSyncedStore(key, value));
      }
    });
    return completer.future;
  }

  Future<SyncedStoreStatus> _putRows(Map<String, String> rows) {
    final Completer<SyncedStoreStatus> completer =
        new Completer<SyncedStoreStatus>();
    _syncedStore.put(
        rows, (final SyncedStoreStatus status) => completer.complete(status));
    return completer.future;
  }

  static SessionGraph _newSessionGraph() {
    return new SessionGraph()
      ..nodes = <NodeRecord>[]
      ..edges = <EdgeRecord>[];
  }
}

class _SyncedStoreObserver extends SyncedStoreObserver {
  final LedgerObserver observer;
  final PathExpression query;
  SyncedStoreObserverStub _stub;

  _SyncedStoreObserver(this.observer, this.query) {
    _stub = new SyncedStoreObserverStub.unbound()..impl = this;
    // TODO(nellyv): remove this when path expressions are supported.
    assert(query == null);
  }

  SyncedStoreObserverStub get stub => _stub;

  @override
  Future<Null> onChange(Map<String, String> changes, void callback()) async {
    Map<GraphUpdateTag, int> sortIndexes = {
      GraphUpdateTag.nodeAdded: 0,
      GraphUpdateTag.edgeAdded: 1,
      GraphUpdateTag.edgeRemoved: 2,
      GraphUpdateTag.nodeRemoved: 3,
      GraphUpdateTag.valueUpdated: 4
    };
    List<GraphUpdate> sortedUpdates = changes.keys
        .map((String key) =>
            SessionRow.fromSyncedStore(key, changes[key]).toGraphUpdate())
        .toList();
    sortedUpdates.sort((GraphUpdate a, GraphUpdate b) {
      return sortIndexes[a.tag] - sortIndexes[b.tag];
    });

    final Completer completer = new Completer();
    observer.onChange(sortedUpdates, () => completer.complete());
    await completer.future;
    callback();
  }
}
