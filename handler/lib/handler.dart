// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/// This file is a collection of functions that handle a session.
///
/// Graph algorithms used here are very straightforward. We will
/// optimize them when we need to.

import 'dart:async';

import 'package:modular_core/util/timeline_helper.dart';
import 'package:modular_core/uuid.dart';
import 'package:parser/manifest.dart' show Manifest;
import 'package:parser/expression.dart';
import 'package:parser/recipe.dart' show Recipe;
import 'package:tuple/tuple.dart';

import 'inspector_json_server.dart';
import 'graph/graph_store.dart';
import 'graph/impl/fake_ledger_graph_store.dart';
import 'graph/impl/lazy_cloned_session_graph.dart';
import 'graph/session_graph.dart';
import 'graph/session_graph_store.dart';
import 'manifest_matcher.dart';
import 'module_runner.dart';
import 'session.dart';

/// The handler keeps the session graph and the known manifests, and
/// implements the matching of data in the session graph against recipe
/// steps and manifests and the computation of module instances, as well
/// as the inserting of module outputs into the session graph.
class Handler implements Inspectable {
  final Map<Uuid, Future<Session>> _sessions = <Uuid, Future<Session>>{};

  final SessionGraphStore graphStore;
  final ModuleRunnerFactory runnerFactory;

  /// All known manifests.
  final ManifestMatcher manifestMatcher;

  final List<Tuple2<SessionStartedCallback, SessionStoppedCallback>>
      _sessionObservers = [];

  final InspectorJSONServer _inspector;

  Handler(
      {final List<Manifest> manifests,
      this.runnerFactory,
      final GraphStore graphStore,
      final InspectorJSONServer inspector})
      : graphStore =
            graphStore ?? new SessionGraphStore(new FakeLedgerGraphStore()),
        manifestMatcher = new ManifestMatcher(manifests),
        _inspector = inspector {
    _inspector?.publish(this);
  }

  Handler.fromManifestMatcher(this.manifestMatcher,
      {this.runnerFactory,
      final GraphStore graphStore,
      final InspectorJSONServer inspector})
      : graphStore =
            graphStore ?? new SessionGraphStore(new FakeLedgerGraphStore()),
        _inspector = inspector {
    _inspector?.publish(this);
  }

  void addSessionObserver(final SessionStartedCallback started,
      final SessionStoppedCallback stopped) {
    // No effect if both arguments are null.
    if (started == null && stopped == null) return;
    _sessionObservers.add(
        new Tuple2<SessionStartedCallback, SessionStoppedCallback>(
            started, stopped));
  }

  /// Returns all Sessions that were added to this Handler. The returned
  /// [Future] completes when all sessions in the list have ben fully
  /// initialized.
  Future<Iterable<Session>> get sessions => Future.wait(_sessions.values);

  Future<Session> findSession(Uuid sessionId) => _sessions[sessionId];

  @override // Inspectable
  final String inspectorPath = '/handler';

  @override // Inspectable
  Future<dynamic> inspectorJSON() async {
    Iterable<Session> sessions = await this.sessions;
    return {
      'type': 'handler',
      'manifests': manifests.map((Manifest m) => _inspector?.manifest(m)),
      'sessions': sessions.map((Session s) => s.inspectorPath),
    };
  }

  @override // Inspectable
  Future<dynamic> onInspectorPost(dynamic json) async {
    if (json is! Map) {
      throw new ArgumentError('Not a JSON dictionary');
    }
    if (json['name'] != 'reload_module') {
      throw new ArgumentError('Unknown command ${json["name"]}');
    }
    final Manifest m = new Manifest.parseYamlString(json['manifest']);

    if (m.display.any((final PathExpr p) => p.toString() == 'root')) {
      return {
        'success': false,
        'reason': 'Cannot refresh module with display \'root\'',
      };
    }

    // Add the updated manifest to the index.
    manifestMatcher.addOrUpdateManifest(m);

    // Reload all module instances from every running session.
    Iterable<Session> sessions = await Future.wait(_sessions.values);
    sessions.forEach((final Session session) =>
        session.reloadStepsWithUrlPattern(json['url_pattern'], m.url));
    // TODO(ksimbili): Return status about reloading, whether the module was
    // successfully reloaded or not.
    return {'success': true, 'reason': 'OK'};
  }

  void _onSessionStarted(final Session session) {
    _sessionObservers.forEach((o) {
      if (o.item1 != null) o.item1(session);
    });
  }

  void _onSessionStopped(final Session session) {
    if (session.isTemporary) {
      _sessions.remove(session.id);
      _inspector?.notify(this);
    }
    _sessionObservers.forEach((o) {
      if (o.item2 != null) o.item2(session);
    });
  }

  List<Manifest> get manifests => manifestMatcher.manifests;

  Future<Session> createSession(final Recipe recipe) {
    return traceAsync('$runtimeType createSession', () async {
      final Uuid id = new Uuid.random();
      final SessionGraph graph = await graphStore.createGraph(id);

      Session session = new Session.fromRecipe(
          id: id,
          recipe: recipe,
          graph: graph,
          handler: this,
          runnerFactory: runnerFactory,
          startedCallback: _onSessionStarted,
          stoppedCallback: _onSessionStopped,
          inspector: _inspector);
      _addSession(id, new Future<Session>.value(session));
      return session;
    });
  }

  Future<Session> forkSession(final Uuid parentSessionId) async {
    assert(parentSessionId != null);
    return traceAsync('$runtimeType createTemporarySession', () async {
      final Session parentSession = await findSession(parentSessionId);
      if (parentSession == null) return null;

      final Uuid id = new Uuid.random();
      final completer = new Completer<Session>();
      _addSession(id, completer.future);

      // We clone the session graph and don't touch the graph store, as we want
      // the new session to be ephemeral.
      final SessionGraph clonedGraph =
          new LazyClonedSessionGraph(parentSession.graph, id);
      final Session session = new Session.fromGraph(
          id: id,
          graph: clonedGraph,
          handler: this,
          runnerFactory: runnerFactory,
          startedCallback: _onSessionStarted,
          stoppedCallback: _onSessionStopped,
          inspector: _inspector);

      completer.complete(session);
      return session;
    });
  }

  Future<Session> restoreSession(Uuid sessionId) {
    assert(sessionId != null);

    final Future<Session> cachedResult = _sessions[sessionId];
    if (cachedResult != null) {
      return cachedResult;
    }

    final completer = new Completer<Session>();
    _addSession(sessionId, completer.future);

    traceAsync('$runtimeType restoreSession', () async {
      SessionGraph graph;
      try {
        graph = await graphStore.findGraph(sessionId);
      } catch (exception, stackTrace) {
        completer.completeError(exception, stackTrace);
        _sessions.remove(sessionId);
        return;
      }

      final Session session = new Session.fromGraph(
          id: sessionId,
          graph: graph,
          handler: this,
          runnerFactory: runnerFactory,
          startedCallback: _onSessionStarted,
          stoppedCallback: _onSessionStopped,
          inspector: _inspector);
      completer.complete(session);
    });

    return completer.future;
  }

  void _addSession(Uuid sessionId, Future<Session> session) {
    assert(!_sessions.keys.contains(sessionId));

    _sessions[sessionId] = session;

    _inspector?.notify(this);
  }
}
