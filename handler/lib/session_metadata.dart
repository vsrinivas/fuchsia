// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:convert';
import 'dart:developer';
import 'dart:typed_data';

import 'package:modular_core/graph/graph.dart';
import 'package:modular_core/uuid.dart' show Uuid;
import 'package:parser/recipe.dart';

import 'bindings.dart';
import 'constants.dart';

/// Callback implemented by classes wanting to observe the changes to recipe.
typedef void RecipeChangeCallback();

/// The SessionMetadata class helps clients interface with non-user-data that
/// still needs to be written to the data store for a session (a [Graph]).
class SessionMetadata {
  final Node _metadataNode;
  final Graph _graph;

  SessionMetadata(this._graph, this._metadataNode);

  /// Extracts the recipe from [_graph]. Returns null if the recipe is
  /// not stored on the graph.
  Recipe getRecipe() {
    return Timeline.timeSync("$runtimeType getRecipe", () {
      final Uint8List repr = _metadataNode.getValue(Constants.recipeLabel);
      if (repr == null) return null;
      return new Recipe.fromJsonString(UTF8.decode(repr));
    });
  }

  /// Saves the current recipe in the graph.
  void setRecipe(final Recipe recipe) {
    return Timeline.timeSync("$runtimeType setRecipe", () {
      _graph.mutate((GraphMutator mutator) {
        mutator.setValue(_metadataNode.id, Constants.recipeLabel,
            UTF8.encode(recipe.toJsonString()) as Uint8List);
      });
    });
  }

  /// Extracts the suggestion ID from the graph, if it is present. This is only
  /// present in sessions that were created to speculatively execute a
  /// suggestion.
  Uuid getLiveSuggestionId() {
    return Timeline.timeSync('$runtimeType getLiveSuggestionId', () {
      final Uint8List repr =
          _metadataNode.getValue(Constants.suggestionIdLabel);
      if (repr == null) return null;
      return Uuid.fromBase64(UTF8.decode(repr));
    });
  }

  Binding getBinding(final String manifestVerb, final Set<Edge> inputEdges) {
    return new Binding(_graph, _metadataNode, manifestVerb, inputEdges);
  }
}
