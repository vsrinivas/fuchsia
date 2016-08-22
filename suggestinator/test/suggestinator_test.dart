// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:async';
import 'dart:convert';
import 'dart:typed_data';

import 'package:handler/constants.dart';
import 'package:modular_core/graph/async_graph.dart';
import 'package:modular_core/graph/graph.dart';
import 'package:modular_core/graph/mem_graph.dart';
import 'package:modular_core/graph/mutation.dart';
import 'package:modular_core/graph/ref.dart';
import 'package:modular_core/uuid.dart';
import 'package:parser/manifest.dart';
import 'package:parser/recipe.dart';
import 'package:suggestinator/event_log.dart';
import 'package:suggestinator/session.dart';
import 'package:suggestinator/session_state_manager.dart';
import 'package:suggestinator/suggestinator.dart';
import 'package:suggestinator/suggestion.dart';
import 'package:suggestinator/suggestion_provider.dart';
import 'package:test/test.dart';

final List<Completer> _suggestionsUpdatedCompleters = [];

Future getSuggestionsUpdatedFuture() {
  final Completer completer = new Completer();
  _suggestionsUpdatedCompleters.add(completer);
  return completer.future;
}

void _onSuggestionsUpdated() {
  for (final Completer c in _suggestionsUpdatedCompleters) {
    c.complete();
  }
  _suggestionsUpdatedCompleters.clear();
}

class TestSessionStateManager implements SessionStateManager {
  int createSessionCount = 0;
  int forkSessionCount = 0;
  int stopSessionCount = 0;
  int updateSessionCount = 0;
  int getSessionGraphCount = 0;

  List<Step> lastAddedSteps;
  Uuid lastForkSessionId;

  final Map<Uuid, AsyncGraph> sessionGraphs = {};

  Completer _updateSessionCompleter;
  Future get updateSessionFuture {
    _updateSessionCompleter = new Completer();
    return _updateSessionCompleter.future;
  }

  Completer _forkSessionCompleter;
  Future get forkSessionFuture {
    _forkSessionCompleter = new Completer();
    return _forkSessionCompleter.future;
  }

  Completer _stopSessionCompleter;
  Future get stopSessionFuture {
    _stopSessionCompleter = new Completer();
    return _stopSessionCompleter.future;
  }

  SessionCallback _onStarted, _onStopped;

  @override
  final Set<Uuid> sessionIds = new Set<Uuid>();

  @override
  void setSessionLifeCycleCallbacks(
      final SessionCallback onStarted, final SessionCallback onStopped) {
    _onStarted = onStarted;
    _onStopped = onStopped;
  }

  @override
  Future<AsyncGraph> getSessionGraph(final Uuid sessionId) async {
    TestAsyncGraph graph = new TestAsyncGraph();

    // Add a metadata node, since the [Session] code expects this to exist.
    await graph.mutateAsync((final GraphMutator m) {
      Node root = m.addNode();
      Node metadataNode = m.addNode();
      m.addEdge(root.id, [Constants.metadataLabel], metadataNode.id);

      // Write a recipe.
      Recipe recipe = new Recipe.parseYamlString('''
          title: I take beer with my lasagna
          verb: beer
          use:
           - beer: http://beer.org/beer
      ''');
      m.setValue(metadataNode.id, Constants.recipeLabel,
          UTF8.encode(recipe.toJsonString()) as Uint8List);
    });

    sessionGraphs[sessionId] = graph;
    getSessionGraphCount++;

    return graph;
  }

  @override
  Future updateSession(final Uuid sessionId, Iterable<Step> stepsToAdd,
      Iterable<Step> stepsToRemove) async {
    updateSessionCount++;
    lastAddedSteps = stepsToAdd;
    _updateSessionCompleter?.complete();
    _updateSessionCompleter = null;
  }

  @override
  Future<Uuid> createSession(final Recipe recipe) async {
    createSessionCount++;
    final Uuid id = new Uuid.random();
    startSession(id);
    return id;
  }

  @override
  Future<Uuid> forkSession(final Uuid parentSessionId) async {
    forkSessionCount++;
    lastForkSessionId = new Uuid.random();
    startSession(lastForkSessionId);
    _forkSessionCompleter?.complete();
    _forkSessionCompleter = null;
    return lastForkSessionId;
  }

  @override
  Future stopSession(final Uuid sessionId) async {
    assert(sessionIds.contains(sessionId));
    stopSessionCount++;
    sessionIds.remove(sessionId);
    _onStopped(sessionId);
    _stopSessionCompleter?.complete();
    _stopSessionCompleter = null;
  }

  void startSession(final Uuid sessionId) {
    assert(!sessionIds.contains(sessionId));
    sessionIds.add(sessionId);
    _onStarted(sessionId);
  }
}

class FailingTestSessionStateManager extends TestSessionStateManager {
  bool getSessionGraphFails = false;
  bool updateSessionFails = false;
  bool forkSessionFails = false;

  @override
  Future<AsyncGraph> getSessionGraph(final Uuid sessionId) async {
    if (getSessionGraphFails) {
      getSessionGraphCount++;
      throw new StateError('$runtimeType: getSessionGraph failed');
    }

    return super.getSessionGraph(sessionId);
  }

  @override
  Future updateSession(final Uuid sessionId, Iterable<Step> stepsToAdd,
      Iterable<Step> stepsToRemove) async {
    if (updateSessionFails) {
      updateSessionCount++;
      throw new StateError('$runtimeType: updateSession failed');
    }

    return super.updateSession(sessionId, stepsToAdd, stepsToRemove);
  }

  @override
  Future<Uuid> forkSession(final Uuid parentSessionId) async {
    if (forkSessionFails) {
      forkSessionCount++;
      throw new StateError('$runtimeType: forkSession failed');
    }

    return super.forkSession(parentSessionId);
  }
}

class TestAsyncGraph extends MemGraph implements AsyncGraph {
  @override // AsyncGraph
  bool get isReady => true;

  @override // AsyncGraph
  Future<Null> waitUntilReady() => new Future.value();

  @override // AsyncGraph
  Future<Null> mutateAsync(MutateGraphCallback fn, {dynamic tag}) {
    super.mutate(fn, tag: tag);
    return new Future.value();
  }

  @override // Graph
  void mutate(MutateGraphCallback fn, {dynamic tag}) =>
      throw new UnsupportedError('Synchronous mutations not supported');
}

class TestSuggestionProvider implements SuggestionProvider {
  final List<Session> sessions = [];
  int sessionChangeCount = 0;
  Session lastChangedSession;
  UpdateCallback updateCallback;

  Completer _addSessionCompleter;

  Future get addSessionFuture {
    _addSessionCompleter = new Completer();
    return _addSessionCompleter.future;
  }

  @override
  void initialize(
      final List<Manifest> manifests, final UpdateCallback callback) {
    updateCallback = callback;
  }

  @override
  void addSession(final Session session) {
    session.addObserver(_onSessionChange);
    sessions.add(session);
    _addSessionCompleter?.complete();
    _addSessionCompleter = null;
  }

  @override
  void removeSession(final Session session) {
    session.removeObserver(_onSessionChange);
    sessions.remove(session);
  }

  void _onSessionChange(final Session session, final GraphEvent event) {
    sessionChangeCount++;
    lastChangedSession = session;
  }
}

class TestSuggestion extends Suggestion {
  TestSuggestion(this.sessionId, [this.displayModule]);

  @override
  final Uuid sessionId;

  @override
  Iterable<NodeId> get sourceEntities => [];

  @override
  bool get createsNewSession => false;

  @override
  final BasicEmbodiment basicEmbodiment =
      new BasicEmbodiment(description: 'This is a test suggestion');

  @override
  final Manifest displayModule;

  @override
  double get relevanceScore => 0.0;

  @override
  int compareRelevance(final Suggestion other) => -1;

  Future<Uuid> apply(final SessionStateManager m) async => sessionId;
}

Node getMetadataNode(final Graph graph) {
  return graph.nodes.singleWhere((final Node n) => n.inEdges
      .any((final Edge e) => e.labels.contains(Constants.metadataLabel)));
}

void main() {
  Suggestinator suggestinator;
  TestSessionStateManager testSessionStateManager;
  FailingTestSessionStateManager failingSessionStateManager;
  TestSuggestionProvider testProvider;

  setUp(() {
    testSessionStateManager = new TestSessionStateManager();
    testProvider = new TestSuggestionProvider();
    suggestinator =
        new Suggestinator(testSessionStateManager, [testProvider], []);
    suggestinator.observer = _onSuggestionsUpdated;
  });

  Function setUpForFailure = () {
    failingSessionStateManager = new FailingTestSessionStateManager();
    testSessionStateManager = failingSessionStateManager;
    testProvider = new TestSuggestionProvider();
    suggestinator =
        new Suggestinator(testSessionStateManager, [testProvider], []);
    suggestinator.observer = _onSuggestionsUpdated;
  };

  group('Basic Suggestinator Tests', () {
    test('Start/stop sessions', () async {
      final Uuid sessionId1 = new Uuid.random();
      final Uuid sessionId2 = new Uuid.random();

      expect(testProvider.sessions.isEmpty, isTrue);

      // New sessions should be propagated to the suggestion providers.
      testSessionStateManager.startSession(sessionId1);
      await testProvider.addSessionFuture;
      testSessionStateManager.startSession(sessionId2);
      await testProvider.addSessionFuture;

      expect(testProvider.sessions.length, equals(2));
      expect(testProvider.sessions[0].id, equals(sessionId1));
      expect(testProvider.sessions[1].id, equals(sessionId2));

      // Session graph changes should trigger the callback with the correct
      // session object.
      final Session session1 = testProvider.sessions[0];
      expect(testProvider.sessionChangeCount, equals(0));
      await session1.graph.mutateAsync((final GraphMutator m) {
        m.addNode();
      });
      expect(testProvider.sessionChangeCount, equals(1));
      expect(testProvider.lastChangedSession, equals(session1));

      // Removing a session should propagate to the suggestion providers and
      // they should be able to successfully remove their session graph
      // observers.
      await testSessionStateManager.stopSession(sessionId1);
      expect(testProvider.sessions.length, equals(1));
      expect(testProvider.sessions[0].id, equals(sessionId2));

      await session1.graph.mutateAsync((final GraphMutator m) {
        m.addNode();
      });
      expect(testProvider.sessionChangeCount, equals(1));
    });

    test('updateSuggestions', () async {
      final Uuid sessionId = new Uuid.random();
      testSessionStateManager.startSession(sessionId);
      await testProvider.addSessionFuture;

      TestSuggestion s1 = new TestSuggestion(sessionId);
      TestSuggestion s2 = new TestSuggestion(sessionId);
      TestSuggestion s3 = new TestSuggestion(sessionId);

      testProvider.updateCallback([s1], []);
      await getSuggestionsUpdatedFuture();
      expect(suggestinator.suggestions, equals([s1]));

      testProvider.updateCallback([s2], []);
      await getSuggestionsUpdatedFuture();
      expect(suggestinator.suggestions, equals([s1, s2]));

      testProvider.updateCallback([s3], [s2.id]);
      await getSuggestionsUpdatedFuture();
      expect(suggestinator.suggestions, equals([s1, s3]));
    });
  });

  group('Speculative Execution Tests', () {
    final String suggestionManifest = '''
        title: foo
        display:
          - card
          - suggestion
          - foo
          - bar
        url: http://foo.io/module
        use:
          - card: http://foo.com/card
          - suggestion: ${Constants.suggestionDisplayLabel}
          - foo: http://foo.com/foo
          - bar: http://foo.com/bar
      ''';
    test('Success', () async {
      final Uuid sessionId = await testSessionStateManager.createSession(null);
      TestSuggestion s = new TestSuggestion(
          sessionId, new Manifest.parseYamlString(suggestionManifest));

      expect(testSessionStateManager.sessionIds.length, equals(1));
      expect(testSessionStateManager.createSessionCount, equals(1));
      expect(testSessionStateManager.forkSessionCount, equals(0));
      expect(testSessionStateManager.stopSessionCount, equals(0));
      expect(testSessionStateManager.updateSessionCount, equals(0));

      // Add the new suggestion that can be displayed dynamically. A new
      // sub-session should get created.
      Future updateSessionFuture = testSessionStateManager.updateSessionFuture;
      await testProvider.addSessionFuture;
      testProvider.updateCallback([s], []);
      await updateSessionFuture;

      expect(testSessionStateManager.sessionIds.length, equals(2));
      expect(testSessionStateManager.forkSessionCount, equals(1));
      expect(testSessionStateManager.updateSessionCount, equals(1));
      expect(testSessionStateManager.lastForkSessionId, isNotNull);

      // The new sub-session should be updated using the manifest above.
      expect(testSessionStateManager.lastAddedSteps.length, equals(1));
      final Step result = testSessionStateManager.lastAddedSteps.first;
      expect(result.url, equals(Uri.parse('http://foo.io/module')));

      // The newly created session should contain the suggestion ID in its
      // metadata.
      final AsyncGraph graph = testSessionStateManager.sessionGraphs[
          testSessionStateManager.lastForkSessionId];
      expect(graph, isNotNull);

      final Node metadataNode = getMetadataNode(graph);
      expect(
          Uuid.fromBase64(
              UTF8.decode(metadataNode.getValue(Constants.suggestionIdLabel))),
          equals(s.id));
    });

    test('Fork session fails', () async {
      setUpForFailure();
      failingSessionStateManager.forkSessionFails = true;

      final Uuid sessionId = await testSessionStateManager.createSession(null);
      TestSuggestion s = new TestSuggestion(
          sessionId, new Manifest.parseYamlString(suggestionManifest));
      await testProvider.addSessionFuture;
      testProvider.updateCallback([s], []);
      await getSuggestionsUpdatedFuture();

      // Update session should never get called and Suggestinator shouldn't
      // crash.
      await testSessionStateManager.updateSessionFuture
          .timeout(new Duration(seconds: 1), onTimeout: () {
        expect(testSessionStateManager.getSessionGraphCount, equals(1));
        expect(testSessionStateManager.forkSessionCount, equals(1));
        expect(testSessionStateManager.stopSessionCount, equals(0));
        expect(testSessionStateManager.updateSessionCount, equals(0));
      });
    });

    test('Suggestion removed before fork session returns', () async {
      final Uuid sessionId = await testSessionStateManager.createSession(null);
      TestSuggestion s = new TestSuggestion(
          sessionId, new Manifest.parseYamlString(suggestionManifest));
      await testProvider.addSessionFuture;
      testProvider.updateCallback([s], []);
      testProvider.updateCallback([], [s.id]);

      // Update session should never get called.
      await testSessionStateManager.updateSessionFuture
          .timeout(new Duration(seconds: 1), onTimeout: () {
        expect(testSessionStateManager.forkSessionCount, equals(1));
        expect(testSessionStateManager.stopSessionCount, equals(1));
        expect(testSessionStateManager.updateSessionCount, equals(0));
      });
    });

    test('Get session graph fails', () async {
      setUpForFailure();

      final Uuid sessionId = await testSessionStateManager.createSession(null);
      TestSuggestion s = new TestSuggestion(
          sessionId, new Manifest.parseYamlString(suggestionManifest));
      await testProvider.addSessionFuture;

      // getSessionGraph will fail when trying to sync the forked session's
      // graph.
      failingSessionStateManager.getSessionGraphFails = true;
      testProvider.updateCallback([s], []);
      await getSuggestionsUpdatedFuture();

      // Update session should never get called and Suggestinator shouldn't
      // crash.
      await testSessionStateManager.updateSessionFuture
          .timeout(new Duration(seconds: 1), onTimeout: () {
        expect(testSessionStateManager.getSessionGraphCount, equals(2));
        expect(testSessionStateManager.forkSessionCount, equals(1));
        expect(testSessionStateManager.stopSessionCount, equals(1));
        expect(testSessionStateManager.updateSessionCount, equals(0));
      });
    });

    test('Suggestion removed before graph is synced', () async {
      final Uuid sessionId = await testSessionStateManager.createSession(null);
      TestSuggestion s = new TestSuggestion(
          sessionId, new Manifest.parseYamlString(suggestionManifest));
      await testProvider.addSessionFuture;
      testProvider.updateCallback([s], []);

      // Wait for forkSession to succeed.
      await testSessionStateManager.forkSessionFuture;
      testProvider.updateCallback([], [s.id]);

      // Update session should never get called and Suggestinator shouldn't
      // crash.
      await testSessionStateManager.updateSessionFuture
          .timeout(new Duration(seconds: 1), onTimeout: () {
        expect(testSessionStateManager.getSessionGraphCount, equals(2));
        expect(testSessionStateManager.forkSessionCount, equals(1));
        expect(testSessionStateManager.stopSessionCount, equals(1));
        expect(testSessionStateManager.updateSessionCount, equals(0));
      });
    });

    test('Update session fails', () async {
      setUpForFailure();
      failingSessionStateManager.updateSessionFails = true;

      final Uuid sessionId = await testSessionStateManager.createSession(null);
      TestSuggestion s = new TestSuggestion(
          sessionId, new Manifest.parseYamlString(suggestionManifest));
      await testProvider.addSessionFuture;
      testProvider.updateCallback([s], []);

      // Wait until the fork session is terminated due to the exception thrown
      // by updateSession.
      await testSessionStateManager.stopSessionFuture;

      expect(testSessionStateManager.getSessionGraphCount, equals(2));
      expect(testSessionStateManager.forkSessionCount, equals(1));
      expect(testSessionStateManager.stopSessionCount, equals(1));
      expect(testSessionStateManager.updateSessionCount, equals(1));
    });
  });

  group('Suggestion Logging', () {
    List<Event> log = [];
    new EventLog().addObserver(log.add);

    test('Added and removed Suggestions', () async {
      final Uuid sessionId = new Uuid.random();
      testSessionStateManager.startSession(sessionId);
      await testProvider.addSessionFuture;

      TestSuggestion s1 = new TestSuggestion(sessionId);
      TestSuggestion s2 = new TestSuggestion(sessionId);
      TestSuggestion s3 = new TestSuggestion(sessionId);

      log.clear();

      testProvider.updateCallback([s1], []);
      await getSuggestionsUpdatedFuture();

      expect(log.length, equals(1));
      expect(log[0].toJson()['type'], equals(EventType.SUGGESTIONS_ADDED));
      expect(
          log[0].toJson()['body'],
          equals([
            {'id': s1.id, 'sessionId': s1.sessionId}
          ]));

      log.clear();

      testProvider.updateCallback([s2], []);
      await getSuggestionsUpdatedFuture();

      expect(log.length, equals(1));
      expect(log[0].toJson()['type'], equals(EventType.SUGGESTIONS_ADDED));
      expect(
          log[0].toJson()['body'],
          equals([
            {'id': s2.id, 'sessionId': s2.sessionId}
          ]));

      log.clear();

      testProvider.updateCallback([s3], [s2.id]);
      await getSuggestionsUpdatedFuture();

      expect(log.length, equals(2));
      expect(log[0].toJson()['type'], equals(EventType.SUGGESTIONS_ADDED));
      expect(
          log[0].toJson()['body'],
          equals([
            {'id': s3.id, 'sessionId': s3.sessionId}
          ]));
      expect(log[1].toJson()['type'], equals(EventType.SUGGESTIONS_REMOVED));
      expect(
          log[1].toJson()['body'],
          equals([
            {'id': s2.id, 'sessionId': s2.sessionId}
          ]));
    });

    test('Selected Suggestion', () async {
      final Uuid sessionId = new Uuid.random();
      testSessionStateManager.startSession(sessionId);
      await testProvider.addSessionFuture;

      TestSuggestion s1 = new TestSuggestion(sessionId);

      testProvider.updateCallback([s1], []);
      await getSuggestionsUpdatedFuture();

      log.clear();

      final Uuid id = await suggestinator.selectSuggestion(s1.id);

      expect(id, isNotNull);
      expect(log.length, equals(1));
      expect(log[0].toJson()['type'], equals(EventType.SUGGESTION_SELECTED));
      expect(
          log[0].toJson()['body'],
          equals({
            'selected': [
              {'id': s1.id, 'sessionId': s1.sessionId}
            ],
            'notSelected': []
          }));
    });

    test('Multiple subscribers', () async {
      List<Event> log2 = [];
      List<Event> log3 = [];
      new EventLog()..addObserver(log2.add)..addObserver(log3.add);

      final Uuid sessionId = new Uuid.random();
      testSessionStateManager.startSession(sessionId);
      await testProvider.addSessionFuture;

      TestSuggestion s1 = new TestSuggestion(sessionId);

      log.clear();
      log2.clear();
      log3.clear();

      testProvider.updateCallback([s1], []);
      await getSuggestionsUpdatedFuture();

      expect(log.length, equals(1));
      expect(log[0].toJson()['type'], equals(EventType.SUGGESTIONS_ADDED));
      expect(
          log[0].toJson()['body'],
          equals([
            {'id': s1.id, 'sessionId': s1.sessionId}
          ]));

      expect(log2.length, log.length);

      expect(log2[0].toJson()['type'], equals(log[0].toJson()['type']));
      expect(log2[0].toJson()['body'], equals(log[0].toJson()['body']));

      expect(log3.length, log.length);

      expect(log3[0].toJson()['type'], equals(log[0].toJson()['type']));
      expect(log3[0].toJson()['body'], equals(log[0].toJson()['body']));
    });
  });
}
