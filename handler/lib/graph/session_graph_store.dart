// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:async';
import 'dart:typed_data';

import 'package:modular_core/util/timeline_helper.dart';
import 'package:modular_core/uuid.dart';

import '../constants.dart';
import 'graph_store.dart';
import 'impl/session_graph_impl.dart';
import 'session_graph.dart';

export 'graph_store.dart';

// TODO (jjosh): Please update this class as speficied in the bug #713
class SessionGraphStore implements GraphStore {
  final Map<Uuid, Future<SessionGraph>> _graphs =
      <Uuid, Future<SessionGraph>>{};
  final GraphStore _store;
  final GraphStorePrefixGenerator _prefixGenerator;

  SessionGraphStore(this._store, {GraphStorePrefixGenerator prefixGenerator})
      : _prefixGenerator = prefixGenerator ?? ((Uuid id) => 'sess-$id');

  @override
  Future<SessionGraph> createGraph(Uuid sessionId) {
    assert(sessionId != null);
    if (_graphs.containsKey(sessionId)) {
      throw new GraphAlreadyExists(
          sessionId, new Future.value(_graphs[sessionId]));
    }

    Completer<SessionGraph> completer = new Completer<SessionGraph>();
    _graphs[sessionId] = completer.future;

    traceAsync('$runtimeType createGraph', () async {
      try {
        final Graph wrapped = await _store.createGraph(sessionId);
        completer.complete(_wrapGraph(sessionId, wrapped));
      } catch (exception, stackTrace) {
        completer.completeError(exception, stackTrace);
      }
    });

    return completer.future;
  }

  @override // GraphStore
  Future<SessionGraph> findGraph(Uuid sessionId) {
    assert(sessionId != null);
    if (_graphs.containsKey(sessionId)) {
      // TODO(jjosh): wrap the returned future to catch potential
      // GraphAlreadyExists errors... these shouldn't be allowed to bother
      // callers of findGraph().
      return _graphs[sessionId];
    }

    final completer = new Completer<SessionGraph>();
    _graphs[sessionId] = completer.future;

    traceAsync('$runtimeType findGraph', () async {
      try {
        final Graph wrapped = await _store.findGraph(sessionId);
        if (wrapped == null) {
          _graphs.remove(sessionId);
          throw new GraphUnavailable(sessionId, 'Does not exist');
        }
        completer.complete(_wrapGraph(sessionId, wrapped));
      } catch (exception, stackTrace) {
        completer.completeError(exception, stackTrace);
      }
    });

    return completer.future;
  }

  SessionGraph _wrapGraph(Uuid sessionId, Graph ledgerGraph) {
    final String prefix = _prefixGenerator(sessionId);
    Edge metadata = _findMetadataEdgeImmediately(ledgerGraph, sessionId);
    assert(metadata != null);
    return new SessionGraphImpl(sessionId, ledgerGraph, metadata, this, prefix);
  }

  static Edge _findMetadataEdgeImmediately(Graph wrapped, Uuid sessionId) {
    assert(wrapped != null && sessionId != null);
    final Iterable<Edge> metadataEdges = wrapped.edges.where((Edge edge) {
      if (!edge.labels.contains(Constants.metadataLabel)) return false;
      final Uint8List idValue =
          edge.target.getValue(Constants.sessionGraphIdLabel);
      assert(idValue != null);
      return (sessionId == new Uuid(idValue));
    });
    assert(metadataEdges.length <= 1);
    return metadataEdges.isEmpty ? null : metadataEdges.first;
  }
}
