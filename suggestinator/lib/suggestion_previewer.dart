// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:async';
import 'dart:convert';
import 'dart:typed_data';

import 'package:handler/constants.dart';
import 'package:modular/builtin_types.dart';
import 'package:modular_core/graph/ref.dart';
import 'package:modular_core/log.dart';
import 'package:modular_core/uuid.dart';
import 'package:parser/expression.dart';
import 'package:parser/manifest.dart';
import 'package:parser/recipe.dart';

import 'session.dart';
import 'session_state_manager.dart';
import 'suggestion.dart';

typedef Future<Session> SessionFactory(final Uuid sessionId);

/// A [SuggestionPreviewer] is used by [Suggestinator] to run a [Suggestion] in
/// preview mode, if it maps to a module that can take on a 'suggestion'
/// display embodiment.
class SuggestionPreviewer {
  static final Logger _log = log('suggestinator.SuggestionPreviewer');

  final Session session;
  final SessionStateManager sessionStateManager;
  final Suggestion suggestion;
  final SessionFactory sessionFactory;

  bool _starting = false;
  bool _cancelled = false;

  Uuid _forkSessionId;
  Uuid get forkSessionId => _forkSessionId;

  SuggestionPreviewer(this.session, this.suggestion, this.sessionStateManager,
      this.sessionFactory) {
    assert(session != null);
    assert(suggestion != null);
    assert(sessionStateManager != null);
    assert(sessionFactory != null);
  }

  /// Creates an ephemeral session for suggestion simulation and runs the
  /// associated with [suggestion].
  Future<bool> start() async {
    if (!suggestion.canDisplayLive) {
      _log.warning('Suggestion does not support previews');
      return false;
    }

    if (_starting) {
      _log.warning('Already starting');
      return false;
    }

    if (_cancelled) {
      _log.warning('Preview cancelled, nothing to do');
      return false;
    }

    // Synthesize values for required module inputs.
    final List<_SynthesizedData> placeHolders = _ReprValueSynthesizer
        .getDataForUnsatisfiedInputs(session, suggestion.displayModule);
    if (placeHolders == null) {
      _log.warning('Could not synthesize one or more required inputs');
      return false;
    }

    _starting = true;

    try {
      // Create a temporary session.
      assert(suggestion.sessionId != null);
      _forkSessionId =
          await sessionStateManager.forkSession(suggestion.sessionId);
      assert(_forkSessionId != null);

      // Check if we were cancelled in the mean time.
      if (_cancelled) {
        _log.warning('Preview was cancelled before session was forked');
        _resetState();
        return false;
      }

      // Wait until the session graph is synchronized and write the suggestion
      // ID to the metadata node.
      final Session forkSession = await sessionFactory(_forkSessionId);
      assert(forkSession != null);
      if (_cancelled) {
        _log.warning('Preview was cancelled while initializing sub-session');
        _resetState();
        return false;
      }

      // Configure the fork session.
      await forkSession.graph.mutateAsync((mutator) {
        // Write the suggestion ID to the metadata node.
        mutator.setValue(
            forkSession.metadataNode.id,
            Constants.suggestionIdLabel,
            UTF8.encode(suggestion.id.toBase64()) as Uint8List);

        // Write the synthesized placeholders into the graph.
        for (final _SynthesizedData data in placeHolders) {
          Node node = mutator.addNode();
          mutator.setValue(node.id, data.repr, data.data);
          mutator.addEdge(forkSession.rootNode.id, data.labels, node.id);
        }
      });
      if (_cancelled) {
        _log.warning('Preview was cancelled while configuring sub-session');
        _resetState();
        return false;
      }

      // Run the module.
      await sessionStateManager.updateSession(_forkSessionId,
          [new Step.fromManifest(suggestion.displayModule)], []);
    } catch (e, stackTrace) {
      _log.severe('Failed to start suggestion preview: $e\n$stackTrace');
      _resetState();
      return false;
    }

    return true;
  }

  void _resetState() {
    stop();
    _cancelled = false;
    _starting = false;
  }

  /// This method can be called at any point to terminate a suggestion preview.
  /// If this is called before the [Future] returned by a call to [start()]
  /// terminates, then this will appropriately cancel that operation.
  Future stop() {
    _cancelled = true;
    Future result;
    if (_forkSessionId != null) {
      _log.info('Stopping fork-session $_forkSessionId for suggestion: '
          '${suggestion.id}');
      result = sessionStateManager.stopSession(_forkSessionId);
      _forkSessionId = null;
    }
    return result ?? new Future.value();
  }
}

class _SynthesizedData {
  final List<String> labels;
  final String repr;
  final Uint8List data;

  _SynthesizedData(this.labels, this.repr, this.data);
}

class _ReprValueSynthesizer {
  static Uint8List _getDateTimeValue() =>
      BuiltinDateTime.write(new DateTime.now());
  static Uint8List _getIntValue() => BuiltinInt.write(1);
  static Uint8List _getStringValue() => BuiltinString.write('Bananas');

  static Uint8List generateValue(final String label) {
    if (label == Constants.dateTimeRepresentationLabel) {
      return _getDateTimeValue();
    }
    if (label == Constants.intRepresentationLabel) {
      return _getIntValue();
    }
    if (label == Constants.stringRepresentationLabel) {
      return _getStringValue();
    }
    return null;
  }

  /// Returns a list of synthesized representation values for unsatisfied inputs
  /// of the module with manifest [m]. Returns null, if any of the unsatisfied
  /// input fields has a representation that we cannot synthesize.
  static List<_SynthesizedData> getDataForUnsatisfiedInputs(
      final Session session, final Manifest m) {
    final List<PathExpr> inputs = session.findUnsatisfiedInputs(m);
    if (inputs.isEmpty) return [];

    // We only synthesize data for simple types with one of the built-in
    // representations.
    final List<_SynthesizedData> results = [];
    for (final PathExpr input in inputs) {
      if (input.isOptional) continue;
      if (input.isNotSimple) return null;

      final Property p = input.properties.single;
      if (p.cardinality.isOptional) continue;
      if (p.representations.isEmpty) return null;

      // Pick the first label and representation.
      final _SynthesizedData result = new _SynthesizedData(
          p.labels.map((final Label l) => l.uri.toString()).toList(),
          p.representations.first.uri.toString(),
          generateValue(p.representations.first.uri.toString()));
      if (result.data == null) return null;
      results.add(result);
    }

    return results;
  }
}
