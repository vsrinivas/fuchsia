// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:async';
import 'dart:typed_data';
import 'dart:core';
import 'dart:math' show min;

import 'package:collection/collection.dart';
import 'package:modular/builtin_types.dart';
import 'package:modular_core/graph/graph.dart';
import 'package:modular_core/graph/mutation.dart';
import 'package:modular_core/graph/query/convert.dart';
import 'package:modular_core/graph/query/query.dart';
import 'package:modular_core/log.dart';
import 'package:modular_core/util/timeline_helper.dart';
import 'package:modular_core/uuid.dart';
import 'package:parser/expression.dart';
import 'package:parser/manifest.dart';
import 'package:parser/recipe.dart' show Recipe, Step;

import 'helpers.dart' as helpers;
import 'session.dart';
import 'session_state_manager.dart';
import 'suggestion.dart';
import 'suggestion_provider.dart';

/// [LegacyDemoProvider] is a [SuggestionProvider] based on the suggestinator
/// that was built to support the Q4 and Lasagna demos. The whole purpose of
/// this is to keep supporting the demos while transitioning the suggestinator
/// bits out of handler.
///
/// TODO(armansito): Remove this file entirely. The relevant bits are mostly in
/// TypeBasedSuggestionGenerator. Some of the logic should be factored out into
/// resolver, the rest can be factored into an InputTypeBasedProvider. The
/// Understandinator bits should go into a separate mojo app called
/// understandinator.
class LegacyDemoProvider implements SuggestionProvider {
  static final Uri kSpeechInputLabel = Uri
      .parse('https://github.com/domokit/modular/wiki/semantic#speech-input');
  static final Uri kStoryLabel =
      Uri.parse('https://github.com/domokit/modular/wiki/semantic#story');
  static final Uri kSessionIdLabel =
      Uri.parse('https://github.com/domokit/modular/wiki/semantic#session-id');
  final Logger _log = log('suggestinator.LegacyDemoProvider');

  // Callback used to notify when suggestions have updated.
  UpdateCallback _updateCallback;

  // The list of all known module manifests.
  final List<Manifest> manifestIndex = [];

  _TypeBasedSuggestionGenerator _typeSuggestions;
  Session _rootSession;

  final Map<Uuid, LegacyDemoSuggestion> _suggestions =
      <Uuid, LegacyDemoSuggestion>{};
  final Map<Uuid, Session> _sessions = <Uuid, Session>{};
  Uuid _mostRecentSession;
  bool _utterancesFirst = false;
  bool _inDefaultState = true;

  final NotUnderstandMuchinator understandinator =
      new NotUnderstandMuchinator();

  LegacyDemoProvider() {
    _typeSuggestions = new _TypeBasedSuggestionGenerator(this);
  }

  @override // SuggestionProvider
  void initialize(
      final List<Manifest> manifests, final UpdateCallback callback) {
    manifestIndex.addAll(manifests);
    _updateCallback = callback;
  }

  @override // SuggestionProvider
  void addSession(final Session session) {
    assert(session != null);
    session.addObserver(_onSessionGraphChange);
    _sessions[session.id] = session;

    // If this is the root session, add some default suggestions.
    // TODO(armansito): The check here is not valid if modular us run with
    // '--recipe' (see https://github.com/domokit/modular/issues/646).
    if (_sessions.length == 1) {
      assert(_rootSession == null);
      _rootSession = session;
      assert(_suggestions.isEmpty);
      final List<LegacyDemoSuggestion> suggestions =
          _typeSuggestions.generateDefaultSuggestions(session);
      suggestions.forEach((final Suggestion s) => _suggestions[s.id] = s);
      _notifySuggestionsUpdated(suggestions, []);
    }

    // Trigger an initial update with existing entities in the session.
    _onSessionEntitiesChanged(session, session.graph.nodes, [], []);
  }

  @override // SuggestionProvider
  void removeSession(final Session session) {
    assert(session != null);
    session.removeObserver(_onSessionGraphChange);
    _sessions.remove(session.id);
    final List<Uuid> removedSuggestions = [];
    _suggestions.values
        .where((final LegacyDemoSuggestion s) => s.session == session)
        .toList()
        .forEach((final LegacyDemoSuggestion s) {
      removedSuggestions.add(s.id);
      _suggestions.remove(s.id);
    });
    _notifySuggestionsUpdated([], removedSuggestions);
  }

  void _onSessionGraphChange(final Session session, final GraphEvent event) {
    Iterable<GraphMutation> coalescedMutations = event.mutations.coalesced;
    Iterable<Node> addedNodes = coalescedMutations
        .where((final GraphMutation mutation) =>
            mutation.type == GraphMutationType.addNode)
        .map((final GraphMutation mutation) =>
            session.graph.node(mutation.nodeId));
    Iterable<Node> removedNodes = coalescedMutations
        .where((final GraphMutation mutation) =>
            mutation.type == GraphMutationType.removeNode)
        .map((final GraphMutation mutation) =>
            session.graph.node(mutation.nodeId));
    Iterable<Node> changedNodes = coalescedMutations
        .where((final GraphMutation mutation) =>
            mutation.type == GraphMutationType.addEdge ||
            mutation.type == GraphMutationType.removeEdge ||
            mutation.type == GraphMutationType.setValue)
        .map((final GraphMutation mutation) =>
            session.graph.node(mutation.nodeId))
        .where((final Node node) =>
            !addedNodes.contains(node) && !removedNodes.contains(node));

    _onSessionEntitiesChanged(session, addedNodes, removedNodes, changedNodes);
  }

  void _removeObsoleteSuggestions(final Session session) {
    List<Uuid> removedSuggestions = [];
    _suggestions.values
        .where((final LegacyDemoSuggestion s) =>
            s.session == session && s.isObsolete)
        .toList()
        .forEach((final LegacyDemoSuggestion s) {
      _suggestions.remove(s.id);
      removedSuggestions.add(s.id);
    });
    _notifySuggestionsUpdated([], removedSuggestions);
  }

  void _onSessionEntitiesChanged(
      final Session session,
      final Iterable<Node> addedNodes,
      final Iterable<Node> changedNodes,
      final Iterable<Node> removedNodes) {
    traceSync('$runtimeType._onSessionEntitiesChanged', () {
      List<LegacyDemoSuggestion> suggestions = <LegacyDemoSuggestion>[];
      final Map<String, String> sessionToSpeechInputs = <String, String>{};
      for (final Node node in addedNodes) {
        Edge speechInputEdge =
            node.singleInEdgeWithLabels([kSpeechInputLabel.toString()]);
        if (speechInputEdge != null) {
          // The representation values in  entities with 'speech-input' type are
          // stored in following format.
          //  - String: <speech-input>
          //  - session-id   : <session id to which the speech-input correspond
          // to The speech-input from laucnher is always recorded under the user
          // root session, even though the input is intended to the current
          // active session. Hence, 'session-id' of the session is also stored
          // by the launcher so that, the speech input suggestions can be
          // applied to the current active session.
          // This assumes that these entities are coming only from root session
          // of the user.
          // TODO(ksimbili): Either enforce that these labels are coming from
          // only root session of the user or remove the expected schema on
          // these nodes.

          // Get to the story node.
          Edge sessionIdEdge = speechInputEdge.origin
              .singleOutEdgeWithLabels([kSessionIdLabel.toString()]);
          assert(sessionIdEdge != null);
          String sessionId = BuiltinString
              .read(sessionIdEdge.target.getValue(BuiltinString.label));
          String speechInput =
              BuiltinString.read(node.getValue(BuiltinString.label));

          _log.info('Speech Input - session: $sessionId, data: $speechInput');
          sessionToSpeechInputs[sessionId] = speechInput;
        } else {
          final List<LegacyDemoSuggestion> typeBasedSuggestions =
              _typeSuggestions.generateSuggestions(
                  session,
                  (final Label l) =>
                      node.inEdgesWithLabels([l.uri.toString()]).isNotEmpty);
          suggestions.addAll(typeBasedSuggestions);
        }
      }

      // Extract entities out of utterances
      // TODO(armansito): This is OK, since graph updates all happen on the same
      // thread, but it is hacky. Asynchronous changes to different sessions in
      // rapid succession will likely make this variable obsolete. We should
      // probably store this in a map (from Session to bool).
      if (sessionToSpeechInputs.isNotEmpty) {
        // We mark utterance suggestions to be ranked higher than all other
        // suggestions. This flag is sticky and won't get unset until the user
        // selects a suggestion.
        // TODO(armansito): Fix this properly in the future by having a context
        // system that allows Suggestinator to be agnostic to entity type
        // (utterance, etc).
        _utterancesFirst = true;
        sessionToSpeechInputs
            .forEach((final String sessionId, final String speechInput) {
          // TODO(armansito): We should instead directly read utterance entities
          // from the session. The Understandinator could write these to the
          // session graph directly.
          final Session session = _sessions[Uuid.fromBase64(sessionId)];
          if (session == null) {
            // Add suggestions only if the session is started. During restore,
            // session need not be started.
            // TODO(ksimbili): On restoring a session on a different device, we
            // cannot show suggestions generated out of speech inputs
            // appropriately. Please figure out a fix for this.
            return;
          }

          // Clear obsolete suggestions for the session that matches this speech
          // input.
          _removeObsoleteSuggestions(session);

          List<UtteranceGroup> utteranceGroups =
              understandinator.process([speechInput]);
          for (final UtteranceGroup group in utteranceGroups) {
            final List<LegacyDemoSuggestion> typeBasedSuggestions =
                _typeSuggestions.generateUtteranceSuggestions(session, group);
            suggestions.addAll(typeBasedSuggestions);
          }
        });
      }

      // Clear the default suggestions once we start adding context-based ones.
      if (suggestions.isNotEmpty && _inDefaultState) {
        _inDefaultState = false;
        List<Uuid> removedSuggestions = _suggestions.values
            .map((final LegacyDemoSuggestion s) => s.id)
            .toList();
        _suggestions.clear();
        _notifySuggestionsUpdated([], removedSuggestions);
      }

      // Clear suggestions that contain a step that has no use.
      _removeObsoleteSuggestions(session);

      // Add all new suggestions while taking care to replace any old suggestions
      // with the same set of steps if we're now able to connect it better with
      // the current session. We replace existing matching suggestions only if
      // the new suggestion scores higher in relevance.
      final List<LegacyDemoSuggestion> suggestionsAdded = [];
      final List<Uuid> suggestionsRemoved = [];
      suggestions.forEach((final LegacyDemoSuggestion suggestion) {
        assert(!suggestion.isEmpty);
        // All suggestions with matching steps.
        final List<LegacyDemoSuggestion> matching = _suggestions.values
            .where(
                (final LegacyDemoSuggestion s) => s.matchesAddSteps(suggestion))
            .toList();

        // Record if there exists a suggestion that has a higher relevance score
        // than the new one we are trying to add.
        bool foundMoreRelevant = false;
        for (final Suggestion s in matching) {
          // [suggestion] is at least as relevant as [s].
          if (suggestion.compareRelevance(s) <= 0) {
            _suggestions.remove(s.id);
            if (!suggestionsAdded.remove(s)) {
              // Only notify the Suggestinator of [s]'s removal if [s] was
              // already known to the Suggestinator and not a suggestion to be
              // introduced.
              suggestionsRemoved.add(s.id);
            }
          } else {
            foundMoreRelevant = true;
          }
        }
        if (!foundMoreRelevant) {
          _suggestions[suggestion.id] = suggestion;
          suggestionsAdded.add(suggestion);
        }
      });

      _notifySuggestionsUpdated(suggestionsAdded, suggestionsRemoved);
    }, arguments: {
      'session_id': '${session.id}',
      'added_count': '${addedNodes.length}',
      'changed_count': '${changedNodes.length}',
      'removed_count': '${removedNodes.length}'
    }); // traceAsync
  }

  void _notifySuggestionsUpdated(
      final Iterable<Suggestion> added, final Iterable<Uuid> removed) {
    if (added.isEmpty && removed.isEmpty) return;
    _updateCallback(added, removed);
  }
}

class LegacyDemoSuggestion extends Suggestion {
  final LegacyDemoProvider provider;
  final String description;
  final Session session;
  final Uri icon;
  final int themeColor;
  final List<Step> addSteps;
  final List<Step> removeSteps;

  @override
  final bool createsNewSession;

  @override
  final Manifest displayModule;

  UtteranceGroup utterance;

  LegacyDemoSuggestion(this.provider, this.session, this.description,
      {this.addSteps,
      this.removeSteps,
      this.icon,
      this.themeColor,
      this.createsNewSession: false,
      this.displayModule});

  @override
  BasicEmbodiment get basicEmbodiment => new BasicEmbodiment(
      description: description, iconUri: icon, themeColor: themeColor);

  @override
  Uuid get sessionId => session.id;

  // TODO(armansito): Implement this
  @override
  Iterable<NodeId> get sourceEntities => [];

  bool get isEmpty =>
      (addSteps == null || addSteps.isEmpty) &&
      (removeSteps == null || removeSteps.isEmpty);

  bool matchesAddSteps(final LegacyDemoSuggestion other) {
    return const UnorderedIterableEquality<Step>()
        .equals(addSteps, other.addSteps);
  }

  // Returns a score that represents this suggestions relevance given the
  // current context. The score value is a floating point number in the range
  // 0...INT_MAX where a higher score implies higher relevance.
  static const double _scoreIncrement = 100.0;
  static const int _maxUtterancePeriodMs = 300000; // 5 minutes
  double get relevanceScore {
    double score = 0.0;

    // If a suggestion modifies the most recent session
    // TODO(armansito): Instead of a single most-recent comparison, we should
    // probably rank all session in the order of recency, and score suggestions
    // for more recently modified sessions higher.
    if (session.id == provider._mostRecentSession &&
        provider._mostRecentSession != provider._rootSession.id) {
      score += _scoreIncrement;
    }

    // A suggestion that modifies the session it is attached to is more relevant
    // than one that creates a new session.
    // TODO(armansito): This currently holds because only the default
    // suggestions create a new session. This will change.
    if (!createsNewSession) score += _scoreIncrement;

    // A suggestion that strictly adds to the session ranks higher than one that
    // swaps something.
    if (removeSteps == null || removeSteps.isEmpty) score += _scoreIncrement;

    // If there is an utterance, add a score based on its recency. We add a
    // multiplier if an utterance was entered in the latest session change.
    if (utterance != null) {
      assert(utterance.entities.isNotEmpty);

      // Just pick one of the entities for the timestamp. Entities within a
      // group should all have a very close timestamp.
      int deltaMsInt = new DateTime.now().millisecondsSinceEpoch -
          utterance.entities.first.timestamp.millisecondsSinceEpoch;

      // Cap the time period at max
      double deltaMs = min(deltaMsInt, _maxUtterancePeriodMs).toDouble();

      // Use a smaller order of magnitude for utterances.
      double increment = _scoreIncrement / 2;
      double ratio = deltaMs.toDouble() / _maxUtterancePeriodMs.toDouble();
      double utteranceScore = increment - (increment * ratio);

      // Scale up if an utterance was just entered.
      if (provider._utterancesFirst) {
        score *= 20.0;

        // If the understandinator linked the utterances together with a
        // confident meaning, then add a higher score.
        // HACK(armansito): it will do this only for the phrases that are
        // relevant to the demo.
        if (utterance.isConfidentQuery) score *= 10.0;
      }

      score += utteranceScore;
    } else if (!provider._utterancesFirst) {
      // If utterances don't come first, then regular entity suggestions matter
      // more.
      score += _scoreIncrement;
    }

    return score;
  }

  bool get isObsolete {
    return addSteps.any((final Step step) {
      final Step matching = session.recipe.steps
          .firstWhere((final Step s) => s.url == step.url, orElse: () => null);
      return matching != null &&
          (removeSteps == null || !removeSteps.contains(matching));
    });
  }

  @override
  int compareRelevance(final Suggestion other) {
    double score1 = other.relevanceScore;
    double score2 = relevanceScore;

    // Shave off an order of magnitude if these aren't both utterances.
    if (utterance == null ||
        (other as LegacyDemoSuggestion).utterance == null) {
      score1 -= score1 % _scoreIncrement;
      score2 -= score2 % _scoreIncrement;
    }

    double diff = score1 - score2;

    return diff == 0 ? 0 : (diff < 0 ? -1 : 1);
  }

  @override
  Future<Uuid> apply(final SessionStateManager sessionStateManager) async {
    if (!createsNewSession) {
      assert(addSteps != null || removeSteps != null);
      await sessionStateManager.updateSession(
          session.id, addSteps, removeSteps);

      provider._utterancesFirst = false;
      provider._mostRecentSession = session.id;

      return session.id;
    }

    // Start a new session with the steps from the suggestion. If the call to
    // [createSession] succeeds, this process will receive an event from the
    // handler and we'll start providing suggestions automatically.
    assert(removeSteps == null);
    final Recipe recipe = new Recipe(addSteps);
    final Uuid sessionId = await sessionStateManager.createSession(recipe);

    provider._utterancesFirst = false;
    provider._mostRecentSession = sessionId;

    return sessionId;
  }

  @override // Object
  String toString() {
    return '$runtimeType(id: $id, '
        'description: $description, '
        'relevanceScore: $relevanceScore, '
        'session: ${session.id}, '
        'createsNewSession: $createsNewSession, '
        'utterance: $utterance, '
        'addSteps: $addSteps, '
        'removeSteps: $removeSteps)';
  }

  @override
  Map<String, dynamic> toJson() => {
        'id': id,
        'sessionId': session.id,
        'addSteps': addSteps ?? [],
        'removeSteps': removeSteps ?? [],
      };
}

typedef bool TypeMatchingFunction(final Label l);

class _TypeBasedSuggestionGenerator {
  final Logger _log = log('handler.TypeBasedSuggestionGenerator');
  final LegacyDemoProvider provider;

  _TypeBasedSuggestionGenerator(this.provider);

  String _getManifestDescription(final Manifest m) {
    return m.title ?? m.verb.toString();
  }

  bool _anyExpressionContainsLabel(
      final List<PathExpr> expressions, bool test(Label label)) {
    return expressions.any((final PathExpr i) =>
        i.properties.last.labels.any((final Label l) => test(l)));
  }

  bool _allInputsAreOptional(final Manifest m) {
    for (final PathExpr expr in m.input) {
      for (final Property prop in expr.properties) {
        if (!prop.cardinality.isOptional) return false;
      }
    }
    return true;
  }

  // Returns null, if the session already contains an identical step.
  Step _createStepFromManifest(final Session session, final Manifest m) {
    return traceSync('$runtimeType._createStepFromManifest()', () {
      final Step newStep = new Step.fromManifest(m);

      // If a step with the same url already exists in session, then the
      // suggestion is probably irrelevant.
      // TODO(armansito): This logic is broken for suggestions that may want to
      // launch a new session with the same recipe as the current session.
      return session.recipe.steps.contains(newStep) ? null : newStep;
    }); // traceSync
  }

  Step _findEquivalentStep(final Iterable<Step> steps, final Step step) {
    return steps.firstWhere((final Step s) => s.isFunctionallyEquivalent(step),
        orElse: () => null);
  }

  // Returns false if the given Manifest has a display embodiment and no step
  // exists in the given session that can compose it.
  bool _canManifestBeComposed(final Session session, final Manifest m) {
    if (m.display.isEmpty) return true;

    final List<Step> steps = <Step>[]
      ..addAll(session.recipe.steps)
      ..addAll(provider._rootSession?.recipe?.steps ?? <Step>[]);

    // Accumulate all compose expressions in the current session.
    //
    // HACK(mesch): We get the compose expressions only off the manifests, but
    // the manifests of the actual module instances in the session are neither
    // stored in the session graph nor available through the handler service.
    // Therefore, we use the module URL to associate the step with the
    // corresponding module manifest in the module index used by suggestinator.
    // This works for all steps created by suggestinator, because they are
    // created from manifests in the first place, and always have a URL. We can
    // do this also for steps read from a recipe, which may or may not have a
    // url field, but we need to obtain the manifest from the handler in that
    // case, which at the time we don't have access to. Right now we just add
    // url fields to recipe yaml files, but it's just a matter of either
    // exposing this in the handler service or writing the manifest to the
    // session graph.
    final List<List<Property>> composePaths = <List<Property>>[];
    for (final Step step in steps) {
      if (step.url == null) {
        _log.info('Step without url cannot be checked for'
            ' compose destination: $step');
        continue;
      }

      for (final Manifest manifest in provider.manifestIndex) {
        if (step.url == manifest.url) {
          composePaths.addAll(step.resolvePaths(manifest.compose));
        }
      }
    }

    // Every display expression in the manifest must match some compose
    // expression in the session in order to be able to be composed.
    //
    // TODO(mesch): Very similar to code in composition_tree.dart.
    //
    // TODO(mesch): Maybe set equality of labels can be relaxed.
    return m.display
        .map((final PathExpr expr) => expr.properties)
        .every((final List<Property> d) {
      return composePaths.any((final List<Property> c) {
        if (d.length != c.length) {
          return false;
        }
        for (int i = 0; i < d.length; i++) {
          if (!const SetEquality<Label>().equals(d[i].labels, c[i].labels)) {
            return false;
          }
        }
        return true;
      });
    });
  }

  // TODO(armansito): These modules exist as a hack around the fact that
  // suggestions cannot write entities into the session yet, so we blacklist
  // them as they make no sense as suggestions. Remove this blacklist when we
  // support that.
  static final List<Uri> _blacklist = <Uri>[
    Uri.parse('https://tq.mojoapps.io/my_brothers_house.mojo'),
    Uri.parse('https://tq.mojoapps.io/brothers_restaurant.mojo'),
    Uri.parse('https://tq.mojoapps.io/sommelier.mojo')
  ];

  // TODO(rosswang): Work out a more consistent way to handle replace
  // suggestions.
  Iterable<LegacyDemoSuggestion> _createReplaceSuggestions(
          final Session session,
          final Iterable<Manifest> matchingManifests,
          final Set<Manifest> nonReplaceManifests) =>
      matchingManifests.expand((m) {
        // Don't create non-compound suggestion from a manifest if it's
        // blacklisted.
        if (_blacklist.contains(m.url)) return [];

        final Step newStep = _createStepFromManifest(session, m);
        if (newStep == null) return [];
        if (!_canManifestBeComposed(session, m)) return [];

        final bool createsSession = session == provider._rootSession;
        final Step existingStep =
            _findEquivalentStep(session.recipe.steps, newStep);
        // TODO(armansito): Handle better descriptions for replacement
        // suggestions.
        final String description = _getManifestDescription(m);

        final Manifest displayModule =
            helpers.hasSuggestionDisplayLabel(m.display) ? m : null;

        if (existingStep != null) {
          // There is already a step in recipe, hence this can only be a replace
          // suggestion.
          if (nonReplaceManifests != null) {
            nonReplaceManifests.remove(m);
          }

          return [
            new LegacyDemoSuggestion(provider, session, description,
                addSteps: [newStep],
                removeSteps: [existingStep],
                createsNewSession: createsSession,
                icon: m.icon,
                themeColor: m.themeColor,
                displayModule: displayModule)
          ];
        }

        // For manifest matching outputs, we show suggestions(replace) only if
        // there is a step in recipe whose inputs/outputs all match with the
        // manifests inputs/outputs.
        // TODO(ksimbili): This is not entirely correct, but seems to be
        // sufficient as of now.
        return [];
      });

  Iterable<LegacyDemoSuggestion> _createAppendSuggestions(
      final Session session, final Iterable<Manifest> appendManifests) {
    final bool createsSession = session == provider._rootSession;
    return _expandToInteractiveManifests(session, appendManifests)
        .expand((final List<Manifest> plan) {
      final Manifest m = plan.last;
      if (_blacklist.contains(m.url) || !_canManifestBeComposed(session, m))
        return [];

      final List<Step> steps =
          plan.map((m) => _createStepFromManifest(session, m)).toList();
      if (steps.any((s) => s == null)) return [];

      final Set<Step> requiredSteps =
          generateAllRequiredSteps(session, [steps.first], steps);

      // We concatenate individual manifest descriptions to
      // describe a compound suggestion.
      List<String> stepDescriptions = requiredSteps.map((final Step s) {
        if (s.url == null) return s.verb.toString();
        return _getManifestDescription(provider.manifestIndex
            .firstWhere((final Manifest m) => m.url == s.url));
      }).toList();

      final Manifest displayModule =
          helpers.hasSuggestionDisplayLabel(m.display) ? m : null;

      return [
        new LegacyDemoSuggestion(provider, session, stepDescriptions.join(', '),
            addSteps: requiredSteps.toList(),
            createsNewSession: createsSession,
            icon: m.icon,
            themeColor: m.themeColor,
            displayModule: displayModule)
      ];
    });
  }

  Iterable<LegacyDemoSuggestion> _createSuggestions(
          final Session session, final Iterable<Manifest> matchingManifests,
          {final bool matchedInput}) =>
      traceSync('$runtimeType._createSuggestions', () {
        if (matchedInput) {
          final Set<Manifest> appendManifests =
              new Set<Manifest>.from(matchingManifests);
          return []
            ..addAll(_createReplaceSuggestions(
                session, matchingManifests, appendManifests))
            ..addAll(_createAppendSuggestions(session, appendManifests));
        } else {
          return _createReplaceSuggestions(session, matchingManifests, null);
        }
      }); // traceSync

  List<LegacyDemoSuggestion> generateSuggestions(
      final Session session, TypeMatchingFunction matchFunc) {
    final List<Manifest> inputMatchingManifests = [];
    final List<Manifest> outputMatchingManifests = [];

    provider.manifestIndex.forEach((final Manifest m) {
      final List<PathExpr> inputExpressions = <PathExpr>[]..addAll(m.input);
      final List<PathExpr> outputExpressions = <PathExpr>[]..addAll(m.output);

      // We show suggestion for those manifest with any of the inputs leaf
      // semantic label is present on the entity.
      if (_anyExpressionContainsLabel(inputExpressions, matchFunc)) {
        inputMatchingManifests.add(m);
      }
      if (_anyExpressionContainsLabel(outputExpressions, matchFunc)) {
        outputMatchingManifests.add(m);
      }
    });

    if (inputMatchingManifests.isEmpty && outputMatchingManifests.isEmpty) {
      return [];
    }

    // TODO(ksimbili): For these manifests we should also check if other
    // required inputs are present in the graph.
    return []
      ..addAll(_createSuggestions(session, inputMatchingManifests,
          matchedInput: true))
      ..addAll(_createSuggestions(session, outputMatchingManifests,
          matchedInput: false));
  }

  // Replaces the path expression suffix with given suffix path expression, only
  // if the suffix can be matched.
  // For example,
  //    exprs: [A-> C], suffix: (B C)  ==> [A->(B C)]
  //    exprs: [A-> (B C)], suffix: (B C)  ==> [A->(B C)]
  //    exprs: [A-> (B C)], suffix: C  ==> cannot be replaced.
  // The last example is not allowed, because it is assumed that exprs are from
  // manifest and returned PathExpr list will be used in recipe. Hence, we can
  // only add more labels then removing any labels from the PathExpr.
  List<PathExpr> _replaceSuffixIfMatchesWith(
      List<PathExpr> exprs, PathExpr suffix) {
    return exprs.map((final PathExpr expr) {
      if (!_outputSatisfiesInput(expr, suffix)) {
        return expr;
      }

      int suffixOffset = expr.length - suffix.length;
      List<Property> properties = expr.properties.sublist(0, suffixOffset)
        ..addAll(suffix.properties);
      return new PathExpr(properties);
    }).toList();
  }

  // Returns a new step after replacing any output expressions in the step with
  // the expr, using the '_replaceSuffixIfMatchesWith' function.
  // Note we cannot remove any labels from the expressions in step, but we can
  // safely add as the handler can add those (extra) labels in recipe to the
  // lables from module.
  Step _replaceMatchingOutputExpression(Step step, PathExpr expr) {
    if (step == null) {
      return null;
    }
    List<PathExpr> outputsNotMatchingExpr = step.output
        .where((final PathExpr o) => !_outputSatisfiesInput(o, expr))
        .toList();

    List<PathExpr> outputsMatchingExpr = step.output
        .where((final PathExpr o) => _outputSatisfiesInput(o, expr))
        .toList();

    return new Step(
        step.scope,
        step.verb,
        step.input,
        outputsNotMatchingExpr
          ..addAll(_replaceSuffixIfMatchesWith(outputsMatchingExpr, expr)),
        step.display,
        step.compose,
        step.url);
  }

  // Returns true if the property contains all labels of other.
  static bool propertyContainsOther(
          final Property property, final Property other) =>
      property.labels.containsAll(other.labels);

  // Returns the list of manifests which can output the set edges needed by the
  // inputExpr.
  List<Manifest> findManifestWithOutputMatching(
          final Session session, final PathExpr inputExpr) =>
      provider.manifestIndex
          .where((final Manifest m) => m.output.any(
              (final PathExpr outputExpr) =>
                  _outputSatisfiesInput(outputExpr, inputExpr)))
          .toList();

  static bool _outputSatisfiesInput(
          final PathExpr output, final PathExpr input) =>
      input.isSuffixOf(output, equality: propertyContainsOther);

  static bool _sessionSatisfiesInput(
      final Session session, final PathExpr input) {
    // HACK(mesch): GraphQuery gratuitously refuses to match if the root segment
    // is repeated. We avoid the assert() here for now, and should fix either
    // graph query or its use here. At this place, it's totally fine to have a
    // repeated expression.
    final GraphQuery q = pathExprToGraphQuery(input);
    return !q.validate() || q.match(session.graph).isNotEmpty;
  }

  static bool _pendingContextSatisfiesInput(
          final Iterable<Manifest> pendingContext, final PathExpr input) =>
      pendingContext.any((final Manifest m) =>
          m.output.any((final PathExpr o) => _outputSatisfiesInput(o, input)));

  Iterable<List<Manifest>> _expandToInteractiveManifests(
      final Session session, final Iterable<Manifest> initialMatches) {
    final Set<Manifest> interactive = new Set();
    final QueueList<List<Manifest>> queue =
        new QueueList.from(initialMatches.map((m) => [m]));
    final Set<List<Manifest>> plans = new Set();

    while (queue.isNotEmpty) {
      final List<Manifest> plan = queue.removeFirst();
      final Manifest m = plan.last;

      // TODO(rosswang): avoid permutations (at least until we have dataflow)
      print(plan.map((m) => m.title));

      if (m.display.isNotEmpty) {
        // interactive; stop searching
        if (interactive.add(m)) plans.add(plan);
      } else if (plan.length < 5) {
        // depth limit
        queue.addAll(provider.manifestIndex
            .where((final Manifest next) =>
                // TODO(rosswang): actually run the conversions too
                // TODO(rosswang): index the context
                // TODO(rosswang): allow diamond patterns

                // no replacement yet
                !plan.contains(next) &&
                // all inputs are satisfied
                next.input.every((final PathExpr i) =>
                    _sessionSatisfiesInput(session, i) ||
                    _pendingContextSatisfiesInput(plan, i)) &&
                // benefits from plan; TODO(rosswang): remove this - this does
                // not cover cases where a derivation from prior context can
                // enable subsequent transformations (could combine with
                // generateAllRequiredSteps)
                next.input.any((final PathExpr i) =>
                    _pendingContextSatisfiesInput(plan, i)))
            .map((next) => new List.from(plan)..add(next)));
      }
    }

    return plans;
  }

  // Returns the set of additional steps required to meet all inputs in the
  // steps about to be added in the suggestions.
  // Note We try to find steps only for the required inputs. Also the input
  // steps are included in the returned set.
  Set<Step> generateAllRequiredSteps(final Session session,
      final Iterable<Step> steps, final Iterable<Step> generatedSteps) {
    return traceSync('$runtimeType.generateAllRequiredSteps', () {
      final Set<Step> requiredSteps =
          new Set<Step>.from([]..addAll(generatedSteps)..addAll(steps));

      steps.forEach((final Step step) {
        step.input.forEach((final PathExpr i) {
          if (i.properties[0].cardinality.isOptional) {
            // If the first property in the path expression is optional then we
            // really don't need this input to be resolved to instantiate the
            // step.
            return;
          }

          if (_sessionSatisfiesInput(session, i)) {
            // Graph already has the data. So no need to find a step to generate
            // this input.
            return;
          }

          // Check in already known/generated steps.
          final List<Step> knownSteps =
              requiredSteps.where((final Step requiredStep) {
            return requiredStep != step &&
                requiredStep.output.any((final PathExpr outputExpr) =>
                    _outputSatisfiesInput(outputExpr, i));
          }).toList();

          if (knownSteps.isNotEmpty) {
            knownSteps.forEach((final Step s) {
              final Step newStep = _replaceMatchingOutputExpression(s, i);
              requiredSteps.remove(s);
              requiredSteps.add(newStep);
            });
            return;
          }

          // Find all the manifests which can output the data needed by the input
          // expression. We also sort such manifests in the order of their input
          // length, so that we have few inputs to resolve recursively.
          final List<Manifest> sortedMatchingManifests =
              findManifestWithOutputMatching(session, i)
                ..sort((Manifest m1, Manifest m2) =>
                    m1.input.length.compareTo(m2.input.length));

          if (sortedMatchingManifests.isEmpty) {
            _log.severe("***** No matching manifest found "
                "while processing input $i of $step");
            return;
          }

          final Step neededStep =
              _createStepFromManifest(session, sortedMatchingManifests[0]);
          if (neededStep == null ||
              _findEquivalentStep(session.recipe.steps, neededStep) != null) {
            // The step which can produce the outputs is already in the session.
            // No need to add it again.
            return;
          }
          if (_findEquivalentStep(requiredSteps, neededStep) != null) {
            // There is already a step in generated steps which is functionally
            // equivalent to the new step.
            return;
          }
          if (neededStep.input.isNotEmpty) {
            // We need to further resolve the new step.
            final Set<Step> recursiveSteps =
                generateAllRequiredSteps(session, [neededStep], requiredSteps);
            requiredSteps.addAll(recursiveSteps);
          } else {
            requiredSteps.add(neededStep);
          }
        });
      });
      return requiredSteps;
    }); // traceSync
  }

  List<LegacyDemoSuggestion> generateUtteranceSuggestions(
      final Session session, final UtteranceGroup utteranceGroup) {
    return traceSync('$runtimeType.generateUtteranceSuggestions', () {
      // We first try to get a multi-step suggestion out of the utteranceGroup.
      // If we can't do that, then we create a regular type-based suggestion for
      // each utterance in the group.
      // HACK(armansito): We only do this for utterance groups that are
      // pre-generated for the demo.
      if (utteranceGroup.isConfidentQuery) {
        final List<Manifest> matchingManifests = utteranceGroup.entities
            .map((final UtteranceEntity e) => provider.manifestIndex
                .firstWhere((final Manifest m) => m.url == e.types.first))
            .where((final Manifest m) => m != null)
            .toList();

        // For the suggestion icon and theme color, we pick the values from the
        // first manifest that declares an icon and/or themeColor.
        // TODO(armansito): Works for now but we really should have better
        // criteria for selecting the "dominant" module whose icon/color should
        // be displayed for the suggestion.
        final Uri icon = matchingManifests
            .firstWhere((final Manifest m) => m.icon != null,
                orElse: () => null)
            ?.icon;
        final int themeColor = matchingManifests
            .firstWhere((final Manifest m) => m.themeColor != null,
                orElse: () => null)
            ?.themeColor;

        // Generate suggestion if there was a match for each entity.
        // TODO(armansito): Note that the steps that are coalesced into a single
        // Suggestion here won't necessarily build a coherent recipe. We do this
        // here because we know that Understandinator will only return confident
        // queries with matching steps, which is a hack.
        assert(matchingManifests.length == utteranceGroup.entities.length);

        final List<Step> newSteps = matchingManifests
            .map((final Manifest m) => _createStepFromManifest(session, m))
            .where((final Step s) => s != null)
            .toList();

        final Set<Step> allSteps =
            generateAllRequiredSteps(session, newSteps, []);

        // Continue if none of the steps are already in the session.
        if (allSteps.isNotEmpty) {
          final List<Step> existingSteps = allSteps
              .map((final Step s) =>
                  _findEquivalentStep(session.recipe.steps, s))
              .where((final Step s) => s != null)
              .toList();
          final LegacyDemoSuggestion suggestion = new LegacyDemoSuggestion(
              provider, session, utteranceGroup.description,
              addSteps: allSteps.toList(),
              removeSteps: existingSteps.isNotEmpty ? existingSteps : null,
              icon: icon,
              themeColor: themeColor);
          suggestion.utterance = utteranceGroup;

          return [suggestion];
        }
      }

      // Default to one regular suggestion per entity.
      final List<LegacyDemoSuggestion> result = <LegacyDemoSuggestion>[];
      for (final UtteranceEntity e in utteranceGroup.entities) {
        final List<LegacyDemoSuggestion> suggestions =
            generateSuggestions(session, (final Label l) => e.hasType(l.uri));
        suggestions.forEach((final LegacyDemoSuggestion s) =>
            s.utterance = new UtteranceGroup(null, [e]));
        result.addAll(suggestions);
      }

      return result;
    }); // traceSync
  }

  // Generates a default set of suggestions with modules that have no required
  // inputs.
  List<LegacyDemoSuggestion> generateDefaultSuggestions(
          final Session session) =>
      _createSuggestions(
              session, provider.manifestIndex.where(_allInputsAreOptional),
              matchedInput: true)
          .toList();
}

// This is the representation of an entity. It's associated with an
// UtteranceEntity.
class EntityRepresentation {
  final Uri type;
  final Uint8List data;

  EntityRepresentation(this.type, this.data);

  @override
  String toString() {
    String repr;
    if (type == BuiltinString.uri) {
      repr = '"${BuiltinString.read(data)}"';
    } else if (type == BuiltinInt.uri) {
      repr = '${BuiltinInt.read(data)}';
    } else if (type == BuiltinDateTime.uri) {
      repr = '${BuiltinDateTime.read(data)}';
    } else {
      repr = data.map((int n) {
        return (n < 16) ? '0' + n.toRadixString(16) : n.toRadixString(16);
      }).join();
    }
    return '<representation: $type/$repr>';
  }
}

/// An [UtteranceEntity] represents a typed entity that was extracted from
/// natural language input.
class UtteranceEntity {
  final Uri _type;
  final EntityRepresentation _value;
  final DateTime timestamp;

  UtteranceEntity(this._type, this._value) : timestamp = new DateTime.now();

  Set<Uri> get types => new Set<Uri>.from([_type]);

  Set<EntityRepresentation> get values =>
      new Set<EntityRepresentation>.from([_value]);

  bool hasType(final Uri type) => _type == type;

  @override // Object
  String toString() => '[$runtimeType $_type | $_value | $timestamp]';
}

/// An [UtteranceGroup] represents a group of utterances that are semantically
/// linked together.
class UtteranceGroup {
  /// A description that best represents what this group of entities mean
  /// together. This is achieved by using an advanced machine learning
  /// algorithm, that was developed by scientists who spent decades studying the
  /// arcane. Most of these scientists are now dead. Those who survived do not
  /// understandinate why.
  String description;

  /// The entities that constitute this group.
  final List<UtteranceEntity> entities;

  UtteranceGroup(this.description, this.entities);

  /// Returns true, if a group of utterances were confidently matched to form a
  /// meaningful query.
  bool get isConfidentQuery => description != null;

  @override // Object
  String toString() => '[$runtimeType entities: $entities desc: $description]';
}

/// [NotUnderstandMuchinator] is responsible for Natural Language Understanding.
/// It extracts typed entities from user input.
class NotUnderstandMuchinator {
  final Logger _log = log('handler.NotUnderstandMuchinator');

  /// Process the given utterance and return typed entities extracted from the
  /// meaning.
  List<UtteranceGroup> process(final List<String> utterances) {
    final List<UtteranceGroup> groups = <UtteranceGroup>[];
    for (final String utterance in utterances) {
      final String sentence = utterance.trim().toLowerCase();

      // See if we can semantically group different entities based on the demo
      // phrases we know.
      final List<UtteranceGroup> understoodGroups = _understandinate(sentence);
      if (understoodGroups.isNotEmpty) {
        groups.addAll(understoodGroups);
      }

      // Also create an individual entity for each work in |sentence|.
      groups.addAll(_lookUpFromTable(sentence)
          .map((final UtteranceEntity e) => new UtteranceGroup(null, [e])));
    }

    return groups;
  }

  // The Lasagna Demo script
  // TODO(armansito): Add all expressions
  static const String _directionsExpr = '(\\bdirections?|get\\s+me\\b)'
      '(?=.*\\bbrother\'?s\\s+(\\bhouse\\b|\\bplace\\b))';
  static const String _brotherSamExpr = 'brother\\s+sam';
  static const String _showWineStoreExpr = 'wine\\s+stores?';
  // Android frequently recognizes the utterance "wines" as "ones", but our NLU
  // system is smart enough to know what the user actually meant.
  static const String _inventoryExpr =
      '((\\bwhich\\b|\\bwhat\\b)\\s+(\\bwines?\\b|\\bones?\\b))|'
      '(\\bwine\\s+list\\b)';
  static const String _complexLasagnaExpr = 'cheap.*lasagna';
  static const String _winePairingExpr = 'wine\\s+pairing';
  List<UtteranceGroup> _understandinate(final String sentence) {
    // HACK(armansito): We make entities explicitly correspond to modules by
    // using the URL field in the manfest. So we hard-code module urls here.
    final List<UtteranceGroup> groups = [];
    if (sentence.contains(new RegExp(_directionsExpr))) {
      groups.add(new UtteranceGroup(
          'Show directions to Brother\'s Restaurant Korean BBQ', [
        new UtteranceEntity(
            Uri.parse('https://tq.mojoapps.io/brothers_restaurant.mojo'), null),
        new UtteranceEntity(
            Uri.parse('https://tq.mojoapps.io/walking_directions.mojo'), null),
        new UtteranceEntity(Uri.parse('https://tq.mojoapps.io/map.flx'), null),
      ]));
    } else if (sentence.contains(new RegExp(_brotherSamExpr))) {
      groups.add(new UtteranceGroup('Get directions to Sam\'s', [
        new UtteranceEntity(
            Uri.parse('https://tq.mojoapps.io/my_brothers_house.mojo'), null),
        new UtteranceEntity(
            Uri.parse('https://tq.mojoapps.io/walking_directions.mojo'), null),
        new UtteranceEntity(Uri.parse('https://tq.mojoapps.io/map.flx'), null),
      ]));
      groups.add(new UtteranceGroup('Order a Lyft to Sam\'s house', [
        new UtteranceEntity(
            Uri.parse('https://tq.mojoapps.io/order_lyft.mojo'), null)
      ]));
    } else if (sentence.contains(new RegExp(_showWineStoreExpr))) {
      groups.add(new UtteranceGroup('See wine stores near you', [
        new UtteranceEntity(
            Uri.parse('https://tq.mojoapps.io/foursquare_search.mojo'), null),
        new UtteranceEntity(
            Uri.parse('https://tq.mojoapps.io/foursquare.flx'), null)
      ]));
    } else if (sentence.contains(new RegExp(_inventoryExpr))) {
      groups.add(new UtteranceGroup('View their wine list', [
        new UtteranceEntity(
            Uri.parse('https://tq.mojoapps.io/wine_store.flx'), null),
      ]));
    } else if (sentence.contains(new RegExp(_complexLasagnaExpr))) {
      groups.add(new UtteranceGroup('Easy Recipes for Healthy Lasagna', [
        new UtteranceEntity(
            Uri.parse('https://tq.mojoapps.io/all_recipes.mojo'), null),
      ]));
      groups.add(new UtteranceGroup(
          'Read common nutritional information for Lasagna', [
        new UtteranceEntity(
            Uri.parse('https://tq.mojoapps.io/nutritional_info.mojo'), null),
      ]));
    } else if (sentence.contains(new RegExp(_winePairingExpr))) {
      groups.add(new UtteranceGroup(
          'See wines from the list that pair with lasagna', [
        new UtteranceEntity(
            Uri.parse('https://tq.mojoapps.io/sommelier.mojo'), null),
      ]));
    } else {
      _log.info('No pre-canned phrase for: "$sentence"');
    }

    // TODO(armansito): Handle the other expressions.

    return groups;
  }

  List<UtteranceEntity> _lookUpFromTable(final String sentence) {
    final List<String> words = sentence.trim().split(new RegExp('\\s+'));
    final List<UtteranceEntity> entities = <UtteranceEntity>[];

    for (final String word in words) {
      List<_LookupTableEntry> matchedEntries = nlpLookupTable[word];
      if (matchedEntries == null) continue;
      for (final _LookupTableEntry entry in matchedEntries) {
        final Uri type = entry.label;
        EntityRepresentation value;
        if (entry.reprLabel != null && entry.value != null) {
          value = new EntityRepresentation(entry.reprLabel, entry.value);
        }
        entities.add(new UtteranceEntity(type, value));
      }
    }

    return entities;
  }
}

typedef Uint8List _ValueFunction();

class _LookupTableEntry {
  final Uri label;
  final Uri reprLabel;
  final _ValueFunction _valueFunc;

  _LookupTableEntry(this.label, this.reprLabel, this._valueFunc) {
    assert(label != null);
  }

  Uint8List get value => _valueFunc();
}

class _StringLookupTableEntry extends _LookupTableEntry {
  _StringLookupTableEntry(Uri label, String inputValue)
      : super(label, BuiltinString.uri, () => BuiltinString.write(inputValue));
}

class _NoValueLookupTableEntry extends _LookupTableEntry {
  _NoValueLookupTableEntry(Uri label) : super(label, null, () => null);
}

// TODO(armansito): Make this less constrained on hardcoded keywords by making
// looking at synonyms of utterances and by using a more generic index that maps
// semantic labels to search data (see issue #562)
final Map<String, List<_LookupTableEntry>> nlpLookupTable = {
  'dinner': [
    new _NoValueLookupTableEntry(Uri
        .parse('https://github.com/domokit/modular/wiki/semantic#restaurant')),
  ],
  'directions': [
    new _NoValueLookupTableEntry(
        Uri.parse('https://github.com/domokit/modular/wiki/semantic#location')),
    new _NoValueLookupTableEntry(
        Uri.parse('https://github.com/domokit/modular/wiki/semantic#route')),
    new _NoValueLookupTableEntry(
        Uri.parse('https://www.wikidata.org/wiki/Q146027'))
  ],
  'house': [
    new _NoValueLookupTableEntry(
        Uri.parse('https://www.wikidata.org/wiki/Q20502')), // house music
    new _StringLookupTableEntry(
        Uri.parse('https://www.wikidata.org/wiki/Q188451'), // music genre
        'House Music')
  ],
  'lasagna': [
    new _StringLookupTableEntry(
        Uri.parse('https://github.com/domokit/modular/wiki/semantic#food'),
        'lasagna')
  ],
  'now': [
    new _LookupTableEntry(
        Uri.parse('https://github.com/domokit/modular/wiki/semantic#time'),
        BuiltinDateTime.uri, () {
      return BuiltinDateTime.write(new DateTime.now());
    }),
  ],
  'tonight': [
    new _LookupTableEntry(
        Uri.parse('https://github.com/domokit/modular/wiki/semantic#time'),
        BuiltinDateTime.uri, () {
      DateTime now = new DateTime.now();
      // 7pm today.
      return BuiltinDateTime
          .write(new DateTime(now.year, now.month, now.day, 19));
    }),
    new _StringLookupTableEntry(
        Uri.parse('https://github.com/domokit/modular/wiki/semantic#song'),
        'Tonight (West Side Story)')
  ],
  'wine': [
    new _NoValueLookupTableEntry(
        Uri.parse('https://github.com/domokit/modular/wiki/semantic#wine'))
  ]
};
