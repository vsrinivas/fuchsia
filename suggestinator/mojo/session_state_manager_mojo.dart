// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:async';
import 'dart:convert';

import 'package:modular_core/graph/async_graph.dart';
import 'package:modular/graph/mojo/remote_async_graph.dart';
import 'package:modular_core/log.dart';
import 'package:modular/modular/graph.mojom.dart';
import 'package:modular/modular/handler.mojom.dart';
import 'package:modular_core/uuid.dart';
import 'package:parser/recipe.dart';
import 'package:suggestinator/session_state_manager.dart';

class SessionStateManagerMojo implements SessionStateManager, SessionObserver {
  static final Logger _log = log('suggestinator.SessionStateManagerMojo');

  final HandlerServiceProxy handlerService;
  final SessionGraphServiceProxy graphService;

  SessionCallback _onStarted, _onStopped;

  @override // SessionStateManager
  void setSessionLifeCycleCallbacks(
      final SessionCallback onStarted, final SessionCallback onStopped) {
    _onStarted = onStarted;
    _onStopped = onStopped;
  }

  @override // SessionStateManager
  final Set<Uuid> sessionIds = new Set<Uuid>();

  SessionStateManagerMojo(this.handlerService, this.graphService) {
    assert(handlerService.ctrl.isBound);
    final SessionObserverStub observerStub = new SessionObserverStub.unbound()
      ..impl = this;
    handlerService.addObserver(observerStub);
  }

  @override // SessionStateManager
  Future<AsyncGraph> getSessionGraph(final Uuid sessionId) async {
    assert(graphService != null);
    assert(graphService.ctrl.isBound);

    GraphProxy graphProxy = new GraphProxy.unbound();
    Completer<HandlerStatus> completer = new Completer<HandlerStatus>();
    graphService.getGraph(sessionId.toBase64(), graphProxy,
        (final HandlerStatus status) => completer.complete(status));
    HandlerStatus status = await completer.future;
    _throwIfError(status, sessionId);

    RemoteAsyncGraph asyncGraph = new RemoteAsyncGraph(graphProxy);
    asyncGraph.metadata.debugName = 'Remote Session Graph';
    await asyncGraph.waitUntilReady();

    return asyncGraph;
  }

  @override // SessionStateManager
  Future updateSession(final Uuid sessionId, Iterable<Step> stepsToAdd,
      Iterable<Step> stepsToRemove) async {
    assert(handlerService != null);
    assert(sessionId != null);
    assert(stepsToAdd != null || stepsToRemove != null);

    Completer<HandlerStatus> completer = new Completer<HandlerStatus>();
    handlerService.updateSession(
        sessionId.toBase64(),
        stepsToAdd?.map((final Step s) => JSON.encode(s.toJson()))?.toList(),
        stepsToRemove?.map((final Step s) => JSON.encode(s.toJson()))?.toList(),
        (final HandlerStatus status) => completer.complete(status));
    HandlerStatus status = await completer.future;
    _throwIfError(status, sessionId);
  }

  @override // SessionStateManager
  Future<Uuid> createSession(final Recipe recipe) async {
    assert(handlerService != null);
    assert(recipe != null);

    Completer<HandlerStatus> completer = new Completer<HandlerStatus>();
    String sessionId;
    handlerService.createSession(recipe.toJsonString(),
        (final HandlerStatus status, final String _sessionId) {
      sessionId = _sessionId;
      completer.complete(status);
    });
    _throwIfError(await completer.future);

    assert(sessionId != null);
    return Uuid.fromBase64(sessionId);
  }

  @override // SessionStateManager
  Future<Uuid> forkSession(final Uuid parentSessionId) async {
    assert(handlerService != null);
    assert(parentSessionId != null);

    _log.info('Forking session: $parentSessionId');
    Completer<HandlerStatus> completer = new Completer<HandlerStatus>();
    String sessionId;
    handlerService.forkSession(parentSessionId.toBase64(),
        (final HandlerStatus status, final String _sessionId) {
      sessionId = _sessionId;
      completer.complete(status);
    });
    _throwIfError(await completer.future, parentSessionId);

    assert(sessionId != null);

    final Uuid forkSessionId = Uuid.fromBase64(sessionId);
    _log.info('Created fork session: $forkSessionId from parent: '
        '$parentSessionId');
    return forkSessionId;
  }

  @override // SessionStateManager
  Future stopSession(final Uuid sessionId) async {
    assert(handlerService != null);
    assert(sessionId != null);

    _log.info('Stopping session: $sessionId');
    Completer<HandlerStatus> completer = new Completer<HandlerStatus>();
    handlerService.stopSession(sessionId.toBase64(),
        (final HandlerStatus status) => completer.complete(status));
    _throwIfError(await completer.future, sessionId);
  }

  @override // SessionObserver
  void onSessionsStarted(final List<String> startedSessionIds) {
    final List<Uuid> newIds =
        startedSessionIds.map((id) => Uuid.fromBase64(id)).toList();
    sessionIds.addAll(newIds);
    if (_onStarted != null) newIds.forEach((id) => _onStarted(id));
  }

  @override // SessionObserver
  void onSessionsStopped(final List<String> stoppedSessionIds) {
    final List<Uuid> oldIds =
        stoppedSessionIds.map((id) => Uuid.fromBase64(id)).toList();
    sessionIds.removeAll(oldIds);
    if (_onStopped != null) oldIds.forEach((id) => _onStopped(id));
  }
}

void _throwIfError(final HandlerStatus status, [Uuid sessionId]) {
  switch (status) {
    case HandlerStatus.ok:
      return;
    case HandlerStatus.invalidSessionId:
      throw new InvalidSessionIdError(sessionId);
    case HandlerStatus.invalidRecipe:
    case HandlerStatus.invalidJson:
      throw new InvalidRecipeError();
    default:
      throw 'Handler error: $status';
  }
}
