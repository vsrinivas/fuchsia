// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:async';

import 'package:modular_core/graph/async_graph.dart';
import 'package:modular_core/uuid.dart';
import 'package:parser/recipe.dart';

/// Error returned when a session ID did not match an existing session.
class InvalidSessionIdError extends StateError {
  InvalidSessionIdError(final Uuid sessionId)
      : super('No session exists with ID \'$sessionId\'');
}

/// Error returned when a given recipe or step is invalid.
class InvalidRecipeError extends StateError {
  InvalidRecipeError() : super('Invalid recipe');
}

/// [SessionStateManager] allows callers to register callbacks to observe
/// sessions as they get started and stopped by the Handler and obtain a handle
/// to their graphs.
typedef SessionCallback(final Uuid sessionId);

abstract class SessionStateManager {
  /// Registers session life-cycle observation callbacks.
  void setSessionLifeCycleCallbacks(
      SessionCallback onStarted, SessionCallback onStopped);

  /// Returns a List of currently started session IDs.
  Set<Uuid> get sessionIds;

  /// Returns an [AsyncGraph] that represents the session graph for the session
  /// with [sessionId]. Throws an [InvalidSessionIdError] if there is no session
  /// with that ID. When the returned [Future] completes, the [AsyncGraph] that
  /// its value contains will be fully synchronized with the remote graph that
  /// it mirrors.
  Future<AsyncGraph> getSessionGraph(Uuid sessionId);

  /// Updates the recipe of the session with [sessionId] based on [stepsToAdd]
  /// and [stepsToRemove]. Throws an [InvalidSessionIdError] if there is no
  /// session with the given ID. Throws an [InvalidRecipeError] if any of the
  /// input steps are malformed.
  Future updateSession(
      Uuid sessionId, Iterable<Step> stepsToAdd, Iterable<Step> stepsToRemove);

  /// Creates a new session from [recipe] and returns the ID of the resulting
  /// session. Throws an [InvalidRecipeError] if [recipe] is malformed.
  Future<Uuid> createSession(Recipe recipe);

  /// Creates a temporary fork from an existing session. This is used by
  /// [Suggestinator] to run suggestions dynamically without modifying the
  /// original session and in a way that doesn't persist any data to the ledger.
  Future<Uuid> forkSession(Uuid parentSessionId);

  /// Stops a running session.
  Future stopSession(Uuid sessionId);
}
