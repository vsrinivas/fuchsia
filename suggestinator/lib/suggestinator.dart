// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:async';

import 'package:modular_core/graph/async_graph.dart';
import 'package:modular_core/log.dart';
import 'package:modular_core/uuid.dart';
import 'package:parser/manifest.dart';

import 'event_logger.dart';
import 'session.dart';
import 'session_state_manager.dart';
import 'suggestion.dart';
import 'suggestion_previewer.dart';
import 'suggestion_provider.dart';

// TODO(armansito): Use UpdateCallback here after changing the mojom
typedef void SuggestionsUpdatedCallback();

/// [Suggestinator] is responsible for tracking and ranking context suggestions.
class Suggestinator {
  static final Logger _log = log('suggestinator.Suggestinator');

  final SessionStateManager sessionStateManager;

  final Map<Uuid, Session> _sessions = {};
  final List<SuggestionProvider> _providers = [];
  final Map<Uuid, Suggestion> _suggestions = {};
  final List<Manifest> _manifestIndex = [];

  // Mapping from Suggestion IDs to [SuggestionPreviewer]s that manage the
  // temporary sub-sessions.
  final Map<Uuid, SuggestionPreviewer> _previewSessions = {};

  // Used to notify code paths that rely on a session graph to be fully sync'd
  // when a session graph is ready to access.
  final Map<Uuid, Completer<Session>> _sessionCompleters = {};

  // Creates and/or returns a Session Future for |sessionId|. The returned
  // Future is not guaranteed to complete if a Session with |sessionId| does not
  // exist.
  Future<Session> _getSessionFuture(final Uuid sessionId) {
    if (_sessions.containsKey(sessionId)) {
      return new Future<Session>.value(_sessions[sessionId]);
    }

    return _sessionCompleters
        .putIfAbsent(sessionId, () => new Completer<Session>())
        .future;
  }

  SuggestionsUpdatedCallback _observer;
  void set observer(final SuggestionsUpdatedCallback callback) {
    _observer = callback;
  }

  /// Returns an unsorted list of all context suggestions.
  Iterable<Suggestion> get suggestions => _suggestions.values;

  final EventLogger eventLog = new EventLogger();

  Suggestinator(this.sessionStateManager,
      Iterable<SuggestionProvider> providers, Iterable<Manifest> manifests) {
    assert(sessionStateManager != null);
    sessionStateManager.setSessionLifeCycleCallbacks(
        _onSessionStarted, _onSessionStopped);
    _providers.addAll(providers);
    _manifestIndex.addAll(manifests);
    for (final SuggestionProvider p in providers) {
      p.initialize(
          new List.unmodifiable(_manifestIndex), _onSuggestionsUpdated);
    }
  }

  // Returns true if we're tracking a session for suggestion generation. Just
  // because a session is running doesn't mean we want to provide suggestions
  // for it, e.g. if it is a forked sub-session for live-suggestions.
  bool canSuggestinateForSession(final Uuid sessionId) =>
      !_previewSessions.values
          .any((final SuggestionPreviewer p) => p.forkSessionId == sessionId);

  /// Applies the suggestion with [suggestionId] and returns the ID of the
  /// session that was modified as a result. Returns null, if a suggestion with
  /// [suggestionId] does not exist.
  Future<Uuid> selectSuggestion(final Uuid suggestionId) async {
    _log.info('selectSuggestion $suggestionId');

    final Suggestion suggestion = _suggestions[suggestionId];
    if (suggestion == null) return null;

    // TODO(dennischeng): Log suggestions not selected here as well.
    eventLog.log(new Event.fromSuggestionSelected(suggestion));

    // Kill the sub-session before applying the suggestion, if any.
    try {
      await _stopLiveSuggestion(suggestionId);
    } catch (e) {
      _log.severe('Failed to stop live suggestion execution: $e');
    }
    return suggestion.apply(sessionStateManager);
  }

  Future _stopLiveSuggestion(final Uuid suggestionId) {
    final SuggestionPreviewer previewer = _previewSessions.remove(suggestionId);
    if (previewer == null) return new Future.value();
    return previewer.stop();
  }

  Future _onSessionStarted(final Uuid sessionId) async {
    _log.info('Session started: $sessionId');

    AsyncGraph graph;
    try {
      graph = await sessionStateManager.getSessionGraph(sessionId);
    } catch (error, stackTrace) {
      _log.info('getSessionGraph failed: $error');
      _sessionCompleters.remove(sessionId)?.completeError(error, stackTrace);
      return;
    }

    assert(graph != null);
    final Session session = new Session(sessionId, graph);
    _sessions[sessionId] = session;
    _sessionCompleters.remove(sessionId)?.complete(session);

    // If this suggestion is a forked sub-session then don't notify the
    // providers since we don't want to generate suggestions for a suggestion.
    if (canSuggestinateForSession(sessionId)) {
      for (final SuggestionProvider p in _providers) p.addSession(session);
    }
  }

  void _onSessionStopped(final Uuid sessionId) {
    _log.info('Session stopped: $sessionId');
    final Session session = _sessions.remove(sessionId);
    if (session != null && canSuggestinateForSession(sessionId)) {
      for (final SuggestionProvider p in _providers) p.removeSession(session);
    }
  }

  Future _onSuggestionsUpdated(
      final Iterable<Suggestion> added, final Iterable<Uuid> removed) async {
    assert(removed.every(_suggestions.containsKey));
    eventLog
      ..log(new Event.fromSuggestionsAdded(added))
      ..log(new Event.fromSuggestionsRemoved(
          removed.map((final Uuid id) => _suggestions[id])));

    // TODO(armansito): Eventually we will perform ranking here. For now we take
    // suggestions from all providers and store them all together in a map,
    // which is how we expose them to clients.

    // The provider implementation could incorrectly include the same UUIDs in
    // both |added| and |removed|. We process |added| first, so that we can
    // remove unwanted UUIDs as we process |removed|.
    if (added.any((s) => removed.contains(s.id))) {
      _log.warning('_onSuggestionsUpdated: Found same suggestion in both'
          ' |added| and |removed|\nADDED: $added, REMOVED: $removed');
    }
    added.forEach((s) {
      assert(canSuggestinateForSession(s.sessionId));
      _suggestions[s.id] = s;
    });
    removed.forEach((id) => _suggestions.remove(id));

    // TODO(armansito): For now, we speculatively run all modules that declare a
    // display embodiment. Eventually this should be up to sys UI: we can expose
    // APIs to start/stop speculative execution on a per-suggestion basis so
    // that UI code can decide how a preview is displayed, e.g. only when the
    // suggestion is in view.
    await _updateSpeculativeExecution(added, removed);

    if (_observer != null) _observer();
  }

  Future _updateSpeculativeExecution(
      final Iterable<Suggestion> added, final Iterable<Uuid> removed) async {
    // Kill temporary sessions of obsolete suggestions.
    for (final Uuid id in removed) await _stopLiveSuggestion(id);

    added
        .where((s) => !removed.contains(s.id) && s.canDisplayLive)
        .forEach(_triggerSpeculativeDisplay);
  }

  Future _triggerSpeculativeDisplay(final Suggestion suggestion) async {
    _log.info('Starting up live suggestion: $suggestion');

    // Make sure we don't already have a speculative session running for this
    // suggestion.
    if (_previewSessions.containsKey(suggestion.id)) {
      _log.warning('Temporary session already running for $suggestion');
      return;
    }

    final SuggestionPreviewer previewer = new SuggestionPreviewer(
        _sessions[suggestion.sessionId],
        suggestion,
        sessionStateManager,
        (final Uuid sessionId) => _getSessionFuture(sessionId));
    _previewSessions[suggestion.id] = previewer;

    bool result = await previewer.start();
    if (!result) {
      _log.warning('Failed to run preview suggestion');
      _previewSessions.remove(suggestion.id);
    }
  }
}
