// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:convert';
import 'dart:typed_data';

import 'package:modular_core/graph/lazy_cloned_graph.dart';
import 'package:modular_core/graph/query/query.dart' show GraphQuery;
import 'package:modular_core/graph/ref.dart';
import 'package:modular_core/log.dart';
import 'package:modular_core/uuid.dart' show Uuid;
import 'package:parser/recipe.dart';

import '../../constants.dart';
import '../session_graph.dart';
import '../session_graph_link.dart';

/// [LazyClonedSessionGraph] can be used to create a temporary fork of an
/// existing session that does not get saved to the ledger.
class LazyClonedSessionGraph extends LazyClonedGraph implements SessionGraph {
  static Logger _log = log('handler.LazyClonedSessionGraph');

  final SessionGraph _baseGraph;

  LazyClonedSessionGraph(final SessionGraph baseGraph, final Uuid sessionId)
      : _baseGraph = baseGraph,
        super(baseGraph) {
    // Clear the recipe node, since we don't want to carry that over,
    mutate((final GraphMutator mutator) {
      mutator.setValue(metadataNode.id, Constants.recipeLabel,
          UTF8.encode(new Recipe([]).toJsonString()) as Uint8List);
    });
  }

  @override // SessionGraph
  Iterable<SessionGraphLink> get links => [];

  @override // SessionGraph
  Node get root => node(_baseGraph.root.id);

  @override // SessionGraph
  Node get metadataNode => node(_baseGraph.metadataNode.id);

  @override // SessionGraph
  SessionGraphLink addSessionLink(Uuid sessionId,
      {GraphQuery query, Node linkOrigin, Iterable<String> labels}) {
    _log.warning('addSessionLink: Session linking is not supported');
    return null;
  }

  @override // SessionGraph
  void removeSessionLink(SessionGraphLink spec) {
    _log.warning('removeSessionLink: Session linking is not supported');
  }
}
