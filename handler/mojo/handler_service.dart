// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:async';
import 'dart:convert';

import 'package:modular_core/log.dart';
import 'package:modular_core/uuid.dart';
import 'package:handler/handler.dart';
import 'package:handler/module_instance.dart';
import 'package:handler/session.dart';
import 'package:modular/modular/handler.mojom.dart';
import 'package:parser/recipe.dart';

export 'package:modular/modular/handler.mojom.dart';

/// Implementation of [HandlerService] defined in handler.mojom.
class HandlerServiceImpl implements HandlerService {
  final Handler _handler;
  final ModuleInstance _instance;
  final List<SessionObserverProxy> _remoteObservers = [];

  static final Logger _log = log('HandlerServiceImpl');

  HandlerServiceImpl(this._handler, this._instance) {
    assert(_handler != null);
    _handler.addSessionObserver((final Session session) {
      _remoteObservers
          .forEach((o) => o.onSessionsStarted([session.id.toBase64()]));
    }, (final Session session) {
      _remoteObservers
          .forEach((o) => o.onSessionsStopped([session.id.toBase64()]));
    });
  }

  @override
  Future<Null> restoreSession(
      final String sessionId, void callback(HandlerStatus status)) async {
    try {
      await (_handler.restoreSession(Uuid.fromBase64(sessionId)))..start();
      callback(HandlerStatus.ok);
    } catch (e) {
      _log.severe('Failed to restore Session $sessionId\n$e');
      callback(HandlerStatus.invalidSessionId);
    }
  }

  @override
  Future<Null> stopSession(
      final String sessionId, callback(HandlerStatus status)) async {
    try {
      await (_handler.restoreSession(Uuid.fromBase64(sessionId)))..stop();
      callback(HandlerStatus.ok);
    } catch (e) {
      _log.severe('Failed to restore Session $sessionId\n$e');
      callback(HandlerStatus.invalidSessionId);
    }
  }

  /// Returns a [HandlerStatus], and also (if the request is sucessful) the ID
  /// of the newly-created Session.
  @override
  Future<Null> createSession(final String recipeText,
      void callback(HandlerStatus status, String sessionId)) async {
    Recipe recipe;
    try {
      if (recipeText == null) {
        recipe = new Recipe([]);
      } else {
        recipe = new Recipe.fromJsonString(recipeText);
      }
      final Session session = (await _handler.createSession(recipe))..start();
      _log.info('Created new Session: ${session.id}');
      callback(HandlerStatus.ok, session.id.toBase64());
    } catch (e) {
      _log.severe('Failed to parse Recipe: $recipeText\n$e');
      callback(HandlerStatus.invalidRecipe, null);
    }
  }

  @override
  Future<Null> forkSession(final String parentSessionId,
      void callback(HandlerStatus status, String sessionId)) async {
    assert(parentSessionId != null);

    final Session session =
        await _handler.forkSession(Uuid.fromBase64(parentSessionId));
    if (session == null) {
      _log.severe('Failed to fork session: $parentSessionId (does not exist)');
      callback(HandlerStatus.invalidSessionId, null);
      return;
    }

    session.start();

    callback(HandlerStatus.ok, session.id.toBase64());
  }

  // Links a session to the current modules graph. The data in the linked
  // session graph will be made available to the module, based on path
  // expressions in the modules manifest.
  @override
  void linkSession(
      final String sessionId, void callback(HandlerStatus status)) {
    // TODO(armansito): For now we fail if this HandlerService wasn't provided
    // to a module (e.g. it was provided to a suggestinator). We should
    // re-define the meaning of this method for the general case when an
    // instance of HandlerService isn't inherently tied to a module/session.
    if (_instance == null) {
      callback(HandlerStatus.notSupported);
      return;
    }

    _instance.session.graph.addSessionLink(Uuid.fromBase64(sessionId));
    callback(HandlerStatus.ok);
  }

  @override
  Future<Null> updateSession(
      final String sessionId,
      final List<String> jsonAddSteps,
      final List<String> jsonRemoveSteps,
      void callback(HandlerStatus status)) async {
    if (jsonAddSteps == null && jsonRemoveSteps == null) {
      _log.severe('Must provide steps to add and/or remove.');
      callback(HandlerStatus.invalidArguments);
      return;
    }
    Session session = await _handler.findSession(Uuid.fromBase64(sessionId));
    if (session == null) {
      _log.severe('Failed to update session: $sessionId (does not exist)');
      callback(HandlerStatus.invalidSessionId);
      return;
    }
    try {
      session.update(
          addSteps: jsonAddSteps?.map((final String jsonStep) =>
              new Step.fromJson(JSON.decode(jsonStep))),
          removeSteps: jsonRemoveSteps?.map((final String jsonStep) =>
              new Step.fromJson(JSON.decode(jsonStep))));
      callback(HandlerStatus.ok);
    } on FormatException catch (e) {
      _log.severe('Failed to parse JSON as Step: $e');
      callback(HandlerStatus.invalidJson);
    }
  }

  @override
  void addObserver(Object observerProxyObject) {
    final SessionObserverProxy observerProxy =
        observerProxyObject as SessionObserverProxy;
    _remoteObservers.add(observerProxy);
    observerProxy.ctrl.errorFuture.then((dynamic error) {
      _log.warning('Observer proxy error: $error');
      _remoteObservers.remove(observerProxy);
    });

    // Send the initial event.
    _handler.sessions.then((final Iterable<Session> sessions) {
      final List<String> startedSessionIds = sessions
          .where((session) => session.isStarted)
          .map((session) => session.id.toBase64())
          .toList();
      if (startedSessionIds.isNotEmpty) {
        observerProxy.onSessionsStarted(startedSessionIds);
      }
    });
  }
}
