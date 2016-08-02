// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:convert';
import 'dart:typed_data';

import 'package:handler/constants.dart';
import 'package:modular_core/graph/async_graph.dart';
import 'package:modular_core/graph/graph.dart';
import 'package:modular_core/graph/mutation.dart';
import 'package:modular_core/graph/query/convert.dart';
import 'package:modular_core/graph/query/query.dart';
import 'package:modular_core/graph/query/query_match_set.dart';
import 'package:modular_core/graph/ref.dart';
import 'package:modular_core/uuid.dart';
import 'package:parser/expression.dart';
import 'package:parser/manifest.dart';
import 'package:parser/recipe.dart';

typedef SessionChangeCallback(final Session session, final GraphEvent event);

/// Represents a user session managed by the session handler. This is a simple
/// class to help associate a session graph with a session ID. This also makes
/// it easier register/unregister a graph callback that has a session ID bound
/// to it.
class Session {
  final Uuid id;
  // AsyncGraph is used here because [Session] is mirroring the graph hosted in
  // the Handler.
  final AsyncGraph graph;

  Recipe _recipe;
  Recipe get recipe => _recipe;

  final Map<SessionChangeCallback, GraphChangeCallback> _observers = {};

  Session(this.id, this.graph) {
    assert(id != null);
    assert(graph != null);
    assert(graph.isReady);
    graph.addObserver(_onGraphChange);
    _loadRecipe();
  }

  void addObserver(final SessionChangeCallback onSessionChange) {
    if (_observers.containsKey(onSessionChange)) return;
    GraphChangeCallback callback =
        (final GraphEvent event) => onSessionChange(this, event);
    graph.addObserver(callback);
    _observers[onSessionChange] = callback;
  }

  void removeObserver(final SessionChangeCallback onSessionChange) {
    GraphChangeCallback callback = _observers.remove(onSessionChange);
    if (callback != null) graph.removeObserver(callback);
  }

  Node get metadataNode {
    // The current session recipe is stored in a special metadata node.
    final GraphQueryMatchSet matches = graph.query(new GraphQuery(
        [Constants.metadataLabel],
        valueLabels: [Constants.recipeLabel]));
    assert(matches.length == 1);
    return matches.single.matchedNodes.single;
  }

  Node get rootNode {
    // The root node can be traced back from the metadata node.
    return metadataNode.inEdges.single.origin;
  }

  /// Given a manifest, returns the subset of its input expressions that cannot
  /// be satisfied by an existing entity in the session.
  List<PathExpr> findUnsatisfiedInputs(final Manifest manifest) {
    assert(manifest != null);
    assert(graph != null);
    assert(graph.isReady);
    final List<PathExpr> results = [];
    for (final PathExpr input in manifest.input) {
      final GraphQuery query = pathExprToGraphQuery(input);
      if (!query.validate() || graph.query(query).isEmpty) {
        results.add(input);
      }
    }
    return results;
  }

  void _loadRecipe() {
    _loadRecipeFromMetadataNode(metadataNode);
  }

  void _onGraphChange(final GraphEvent event) {
    // Update the session recipe if it changed. Iterable.singleWhere throws
    // StateError if the element doesn't exist.
    try {
      final Node changedMetadataNode = event.mutations.coalesced
          .where(
              (final GraphMutation m) => m.type == GraphMutationType.setValue)
          .map((final GraphMutation m) => graph.node(m.nodeId))
          .singleWhere((final Node n) =>
              n.inEdges.any((final Edge e) =>
                  e.labels.contains(Constants.metadataLabel)) &&
              n.valueKeys.contains(Constants.recipeLabel));
      assert(changedMetadataNode != null);
      _loadRecipeFromMetadataNode(changedMetadataNode);
    } catch (e) {}
  }

  void _loadRecipeFromMetadataNode(final Node metadataNode) {
    assert(metadataNode != null);
    final Uint8List repr = metadataNode.getValue(Constants.recipeLabel);
    _recipe = new Recipe.fromJsonString(UTF8.decode(repr ?? []));
  }
}
