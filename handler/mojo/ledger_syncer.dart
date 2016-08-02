// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:async';
import 'dart:typed_data';

import 'package:collection/collection.dart';
import 'package:common/serializer.dart';
import 'package:modular_core/uuid.dart';
import 'package:meta/meta.dart';
import 'package:modular_core/graph/graph.dart';
import 'package:modular_core/graph/mem_graph.dart';
import 'package:modular_core/graph/mutation_utils.dart';
import 'package:modular_core/util/timeline_helper.dart';
import 'package:modular_services/ledger/ledger.mojom.dart' as ledger;
import 'package:tuple/tuple.dart';

import 'errors.dart';

/// Callback from the ledger observer to the ledger graph.
typedef void _OnLedgerChange(List<ledger.GraphUpdate> changes);

class _LedgerObserverImpl implements ledger.LedgerObserver {
  final _OnLedgerChange _callback;

  _LedgerObserverImpl(this._callback);

  @override
  void onChange(List<ledger.GraphUpdate> changes, void mojoCallback()) {
    _callback(changes);
    mojoCallback();
  }
}

/// Bi directional map from local graph entity to remote graph entity.
abstract class _EntityMapping<Local, Remote> {
  final Map<String, Tuple2<Local, Remote>> _idToEntity =
      <String, Tuple2<Local, Remote>>{};

  void add(final Local local, final Remote remote) {
    assert(local != null);
    assert(remote != null);
    assert(!_idToEntity.containsKey(getLocalId(local)));
    _idToEntity[getLocalId(local)] = new Tuple2<Local, Remote>(local, remote);
  }

  void addAll(_EntityMapping<Local, Remote> other) {
    _idToEntity.addAll(other._idToEntity);
  }

  Local getLocalById(final String id) => _idToEntity[id]?.item1;

  Remote getRemoteById(final String id) => _idToEntity[id]?.item2;

  Local getLocal(final Remote remote) => getLocalById(getId(remote));

  Remote getRemote(final Local local) => getRemoteById(getLocalId(local));

  void _remove(final String id) {
    _idToEntity.remove(id);
  }

  void removeByLocal(final Local local) {
    _remove(getLocalId(local));
  }

  void removeByRemote(final Remote remote) {
    _remove(getId(remote));
  }

  Iterable<Local> get locals =>
      _idToEntity.values.map((Tuple2<Local, Remote> pair) => pair.item1);
  Iterable<Remote> get remotes =>
      _idToEntity.values.map((Tuple2<Local, Remote> pair) => pair.item2);

  @protected
  String getId(Remote remote);

  @protected
  String getLocalId(Local local);
}

/// Entity mapping for nodes.
class _NodeMapping extends _EntityMapping<Node, ledger.NodeRecord> {
  @override
  String getId(final ledger.NodeRecord nodeRecord) => nodeRecord.nodeId.id;

  @override
  String getLocalId(final Node node) => node.id.toString();
}

/// Entity mapping for edges.
class _EdgeMapping extends _EntityMapping<Edge, ledger.EdgeRecord> {
  @override
  String getId(final ledger.EdgeRecord edgeRecord) => edgeRecord.edgeId.id;

  @override
  String getLocalId(final Edge edge) => edge.id.toString();
}

/// The syncer observes both a ledger session and a local graph and forward
/// changes in both direction.
class LedgerSyncer {
  final MemGraph _graph;
  final ledger.Ledger _ledger;
  final ledger.LedgerObserverStub _observerStub;

  /// Initialization is asynchronous. Client must wait for init to finish
  //before mutating the graph.
  bool _initDone;

  /// Nodes and edges collections.
  final _NodeMapping _nodes = new _NodeMapping();
  final _EdgeMapping _edges = new _EdgeMapping();

  /// Id of the current session.
  ledger.SessionId _sessionId;

  /// Coalesce mutations when receiving those while a call to the syncstore is
  /// in progress.
  CoalescedMutations _coalescedMutations = new CoalescedMutations();

  /// Serialize all async calls.
  bool _inSerializer = false;
  Serializer _serializer = new Serializer();

  LedgerSyncer(this._graph, this._ledger)
      : _observerStub = new ledger.LedgerObserverStub.unbound() {
    /// For now, the syncer can only operate on an empty graph.
    assert(this._graph.nodes.length == 0);
    assert(this._graph.edges.length == 0);
    this._graph.addObserver(_onGraphChange);
  }

  Uuid get sessionId => Uuid.fromBase64(_sessionId.id);

  /// The init method must be called, and must be finished before any
  /// modification on the graph is done.
  Future<Null> init() async {
    return traceAsync("$runtimeType init", () {
      return _serializer.execute(() async {
        Completer<Null> completer = new Completer<Null>();
        _ledger.createSession((final ledger.SessionId sessionId,
            final ledger.LedgerStatus status) {
          if (status != ledger.LedgerStatus.ok) {
            completer.completeError(new LedgerGraphException(
                "Unable to create a new session. Status: ${status}"));
          } else {
            _sessionId = sessionId;
            _initObserver();
            _initDone = true;
            completer.complete();
          }
        });
        return completer.future;
      });
    });
  }

  Future<Null> initWithSession(Uuid sessionId,
      {bool createIfMissing: false}) async {
    return traceAsync("$runtimeType initWithSession", () {
      return _serializer.execute(() async {
        Completer<Null> completer = new Completer<Null>();
        _sessionId = new ledger.SessionId()..id = sessionId.toBase64();
        _ledger.getSessionGraph(_sessionId,
            new ledger.LedgerOptions()..createIfMissing = createIfMissing,
            (final ledger.SessionGraph sessionGraph,
                final ledger.LedgerStatus status) {
          if (status != ledger.LedgerStatus.ok) {
            completer.completeError(
                new LedgerGraphException("Unable to find session $sessionId."));
          } else {
            _initObserver();

            _initDone = true;

            // Represent the result as a list of [GraphUpdate]s which can be
            // [nodeAdded], [edgeAdded] or [valueUpdated].
            final List<ledger.GraphUpdate> graphAsUpdates =
                <ledger.GraphUpdate>[]
                  ..addAll(sessionGraph.nodes.map((ledger.NodeRecord node) =>
                      new ledger.GraphUpdate()..nodeAdded = node))
                  ..addAll(sessionGraph.edges.map((ledger.EdgeRecord edge) =>
                      new ledger.GraphUpdate()..edgeAdded = edge))
                  ..addAll(sessionGraph.nodes.expand((ledger.NodeRecord node) =>
                      _toValueUpdates(node.representationValues)));
            _onLedgerChange(graphAsUpdates);
            completer.complete();
          }
        });

        return completer.future;
      });
    });
  }

  Iterable<ledger.GraphUpdate> _toValueUpdates(
      List<ledger.Representation> values) {
    if (values == null) {
      return const Iterable<ledger.GraphUpdate>.empty();
    }
    return values.map((ledger.Representation value) =>
        new ledger.GraphUpdate()..valueUpdated = value);
  }

  Future<Null> close({bool immediate: false}) {
    return _observerStub.close(immediate: immediate);
  }

  Future<Null> _initObserver() async {
    _observerStub.impl = new _LedgerObserverImpl(_onLedgerChange);
    final Completer<ledger.LedgerStatus> completer =
        new Completer<ledger.LedgerStatus>();
    _ledger.addObserver(_sessionId, null, _observerStub,
        (final ledger.LedgerStatus status) => completer.complete(status));
    final ledger.LedgerStatus status = await completer.future;
    if (status != ledger.LedgerStatus.ok) {
      throw new LedgerGraphException(
          "Unable to register an observer on the session.");
    }
  }

  ledger.NodeRecord _newRecordFrom(ledger.NodeRecord remoteNode) {
    return new ledger.NodeRecord()
      ..nodeId = remoteNode.nodeId
      ..creationDevice = remoteNode.creationDevice
      ..creationUser = remoteNode.creationUser
      ..deleted = remoteNode.deleted
      ..representationValues = new List<ledger.Representation>.from(
          remoteNode?.representationValues ?? <ledger.Representation>[]);
  }

  void _onLedgerChange(final List<ledger.GraphUpdate> graphUpdates) {
    traceSync("LedgerSyncer _onLedgerChange", () {
      assert(_initDone);

      // Wrap all local Graph modifications in setState() so that all
      // mutation events are bundled into one observer notification.
      _graph.mutate((GraphMutator mutator) {
        _onLedgerChangeBody(mutator, graphUpdates);
      }, tag: this);
    });
  }

  void _onLedgerChangeBody(
      GraphMutator mutator, final List<ledger.GraphUpdate> graphUpdates) {
    for (final ledger.GraphUpdate graphUpdate in graphUpdates) {
      switch (graphUpdate.tag) {
        case ledger.GraphUpdateTag.nodeAdded:
          {
            final ledger.NodeRecord remoteNode = graphUpdate.nodeAdded;
            Node localNode = _nodes.getLocal(remoteNode);
            if (localNode == null) {
              final NodeId localId =
                  new NodeId.fromString(remoteNode.nodeId.id);
              mutator.apply(new GraphMutation.addNode(localId));
              localNode = _graph.nodeRefFactory(localId);
              _nodes.add(localNode, _newRecordFrom(remoteNode));
            }
          }
          break;
        case ledger.GraphUpdateTag.nodeRemoved:
          {
            final Node localNode =
                _nodes.getLocalById(graphUpdate.nodeRemoved.id);
            if (localNode != null) {
              mutator.removeNode(localNode.id);
              _nodes.removeByLocal(localNode);
            }
          }
          break;
        case ledger.GraphUpdateTag.edgeAdded:
          {
            ledger.EdgeRecord remoteEdge = graphUpdate.edgeAdded;
            if (_edges.getLocal(remoteEdge) != null) {
              // Edge is already accounted for.
              break;
            }
            final Node startLocalNode =
                _nodes.getLocalById(remoteEdge.start.id);
            final Node endLocalNode = _nodes.getLocalById(remoteEdge.end.id);
            assert(startLocalNode != null);
            assert(endLocalNode != null);
            final Iterable<String> labels =
                remoteEdge.labels.map((final ledger.LabelUri uri) => uri.uri);
            final EdgeId localId = new EdgeId.fromString(remoteEdge.edgeId.id);
            mutator.apply(new GraphMutation.addEdge(
                localId, startLocalNode.id, endLocalNode.id, labels));
            final Edge localEdge =
                new Edge(localId, startLocalNode, endLocalNode, labels);
            _edges.add(localEdge, remoteEdge);
          }
          break;
        case ledger.GraphUpdateTag.edgeRemoved:
          {
            final Edge localEdge =
                _edges.getLocalById(graphUpdate.edgeRemoved.id);
            if (localEdge != null) {
              mutator.removeEdge(localEdge.id);
              _edges.removeByLocal(localEdge);
            }
          }
          break;
        case ledger.GraphUpdateTag.valueUpdated:
          {
            final ledger.Representation newValue = graphUpdate.valueUpdated;
            final ledger.NodeRecord remoteNode =
                _nodes.getRemoteById(newValue.nodeId.id);
            final Node localNode = _nodes.getLocal(remoteNode);

            final ledger.Representation cachedValue =
                remoteNode.representationValues?.firstWhere(
                    (ledger.Representation oldValue) =>
                        oldValue.label.uri == newValue.label.uri,
                    orElse: () => null);
            // Check the timestamps first, if the change from the Ledger was
            // already replaced by a local change, ignore it.
            if (cachedValue != null &&
                cachedValue.timestamp > newValue.timestamp) {
              break;
            }
            if (newValue.value == null) {
              mutator.setValue(localNode.id, newValue.label.uri, null);
            } else {
              mutator.setValue(localNode.id, newValue.label.uri,
                  new Uint8List.fromList(newValue.value));
            }
            if (cachedValue == null) {
              remoteNode.representationValues ??= [];
              remoteNode.representationValues.add(newValue);
            } else {
              cachedValue.value = newValue.value;
              cachedValue.timestamp = newValue.timestamp;
            }
          }
          break;
        default:
          // Unreachable statement.
          throw new LedgerGraphException(
              "Illegal GraphUpdate type for: $graphUpdate");
      }
    }
  }

  void _onGraphChange(final GraphEvent event) {
    traceSync('$runtimeType._onGraphChange()', () {
      assert(_initDone);

      final List<GraphMutation> mutations =
          event.mutations.coalesced.withoutTag(this).toList();
      if (mutations.isEmpty) return;

      // TODO(qsr): Write a test for serializer and coalesced mutations.
      if (_inSerializer) {
        for (final GraphMutation mutation in mutations) {
          _coalescedMutations.appendMutation(mutation);
        }
        return;
      }

      _applyMutations(event.graph, mutations);
    });
  }

  void _applyMutations(
      final Graph graph, final Iterable<GraphMutation> mutations) {
    return traceSync("LedgerSyncer _applyMutations", () {
      final List<ledger.NodeRecord> newNodes = [];
      final List<ledger.EdgeRecord> newEdges = [];
      final List<ledger.Representation> newRepresentations = [];
      final List<ledger.NodeId> deletedNodes = [];
      final List<ledger.EdgeId> deletedEdges = [];
      final int timestamp = new DateTime.now().millisecondsSinceEpoch;

      for (final GraphMutation mutation in mutations) {
        switch (mutation.type) {
          case GraphMutationType.addEdge:
            {
              ledger.EdgeRecord remoteEdge =
                  _edges.getRemoteById(mutation.edgeId.toString());

              if (remoteEdge != null) {
                // The edge already exist.
                continue;
              }
              Edge localEdge = graph.edge(mutation.edgeId);
              if (localEdge == null) {
                // The edge is not in the graph anymore.
                continue;
              }
              ledger.NodeRecord startRemoteNode =
                  _nodes.getRemoteById(mutation.originNodeId.toString());
              assert(startRemoteNode != null);
              ledger.NodeRecord endRemoteNode =
                  _nodes.getRemoteById(mutation.targetNodeId.toString());
              assert(endRemoteNode != null);
              remoteEdge = new ledger.EdgeRecord()
                ..edgeId = (new ledger.EdgeId()..id = localEdge.id.toString())
                ..start = startRemoteNode.nodeId
                ..end = endRemoteNode.nodeId
                ..labels = new List<ledger.LabelUri>.from(localEdge.labels.map(
                    (final String label) =>
                        new ledger.LabelUri()..uri = label));
              _edges.add(localEdge, remoteEdge);
              newEdges.add(remoteEdge);
            }
            break;
          case GraphMutationType.removeEdge:
            {
              ledger.EdgeRecord remoteEdge =
                  _edges.getRemoteById(mutation.edgeId.toString());
              if (remoteEdge == null) {
                // The edge is alredy deleted.
                continue;
              }
              deletedEdges.add(remoteEdge.edgeId);
              _edges.removeByRemote(remoteEdge);
            }
            break;
          case GraphMutationType.addNode:
            {
              if (_nodes.getRemoteById(mutation.nodeId.toString()) != null) {
                // The node already exists.
                continue;
              }
              Node localNode = graph.node(mutation.nodeId);
              if (localNode == null) {
                // The node is not in the graph anymore.
                continue;
              }
              final ledger.NodeRecord remoteNode = new ledger.NodeRecord()
                ..nodeId = (new ledger.NodeId()..id = localNode.id.toString());

              // Add representation values.
              remoteNode.representationValues =
                  localNode.valueKeys.isEmpty ? null : [];
              for (String key in localNode.valueKeys) {
                remoteNode.representationValues.add(new ledger.Representation()
                  ..nodeId = remoteNode.nodeId
                  ..label = (new ledger.LabelUri()..uri = key)
                  ..timestamp = timestamp
                  ..value = localNode.getValue(key));
              }

              _nodes.add(localNode, remoteNode);
              newNodes.add(remoteNode);
            }
            break;
          case GraphMutationType.removeNode:
            {
              ledger.NodeRecord remoteNode =
                  _nodes.getRemoteById(mutation.nodeId.toString());
              if (remoteNode == null) {
                // The node is already deleted.
                continue;
              }
              deletedNodes.add(remoteNode.nodeId);
              _nodes.removeByRemote(remoteNode);
            }
            break;
          case GraphMutationType.setValue:
            {
              Node localNode = _nodes.getLocalById(mutation.nodeId.toString());
              ledger.NodeRecord remoteNode = _nodes.getRemote(localNode);
              assert(remoteNode != null);

              final Set<String> remoteLabels = remoteNode.representationValues
                      ?.map((ledger.Representation rep) => rep.label.uri)
                      ?.toSet() ??
                  new Set<String>();
              final Set<String> localLabels = localNode.valueKeys.toSet();
              final Set<String> toRemove = remoteLabels.difference(localLabels);

              localNode.valueKeys.forEach((String key) {
                final Uint8List value = localNode.getValue(key);
                if (remoteNode.representationValues != null &&
                    remoteNode.representationValues.any((ledger
                            .Representation rep) =>
                        rep.label.uri == key &&
                        const ListEquality<int>().equals(rep.value, value))) {
                  // The value has not changed, no need to do anything.
                  return;
                }
                final ledger.Representation valueUpdate =
                    new ledger.Representation()
                      ..nodeId = remoteNode.nodeId
                      ..label = (new ledger.LabelUri()..uri = key)
                      ..timestamp = timestamp
                      ..value = value;
                remoteNode.representationValues ??= [];
                remoteNode.representationValues.add(valueUpdate);
                newRepresentations.add(valueUpdate);
              });

              for (String label in toRemove) {
                remoteNode.representationValues.remove(label);
                final ledger.Representation valueUpdate =
                    new ledger.Representation()
                      ..nodeId = remoteNode.nodeId
                      ..label = (new ledger.LabelUri()..uri = label)
                      ..timestamp = timestamp
                      ..value = null;
                newRepresentations.add(valueUpdate);
              }
            }
            break;
        }
      }
      //_inSerializer = true;
      _serializer.execute(() {
        try {
          return traceAsync("LedgerSyncer _onGraphChange updateSessionGraph",
              () async {
            final Completer<ledger.LedgerStatus> completer =
                new Completer<ledger.LedgerStatus>();
            _ledger.updateSessionGraph(
                _sessionId,
                newNodes,
                newEdges,
                newRepresentations,
                deletedNodes,
                deletedEdges,
                (final ledger.LedgerStatus status) =>
                    completer.complete(status));
            if (await completer.future != ledger.LedgerStatus.ok) {
              throw new LedgerGraphException("Unable to update the ledger.");
            }
          });
        } finally {
          _inSerializer = false;
        }
        if (!_coalescedMutations.isEmpty) {
          final List<GraphMutation> newMutations = _coalescedMutations.toList();
          _coalescedMutations.clear();
          _applyMutations(graph, newMutations);
          return new Future<dynamic>.value();
        }
      });
    });
  }
}
