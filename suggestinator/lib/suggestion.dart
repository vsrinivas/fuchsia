// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:async';
import 'dart:core';

import 'package:modular_core/graph/id.dart';
import 'package:modular_core/uuid.dart';
import 'package:parser/manifest.dart';

import 'session_state_manager.dart';

/// A [Suggestion] represents a potential action a user can take based on a set
/// of entities that are in the user's session. The action that a suggestion
/// represents can take various forms, the most common of which are:
///
///    1. Directly modifying data that is in the session;
///    2. Running modules that can perform a specific action, this potentially
///       indirectly modifies data that is in the session;
///    3. A combination of (1) and (2) above.
abstract class Suggestion {
  /// UUID that uniquely identifies this suggestion. Clients of a suggestinator
  /// will use this UUID to refer to this suggestion when, for example, applying
  /// this suggestion.
  final Uuid id = new Uuid.random();

  /// UUID of the session that this [Suggestion] is for.
  Uuid get sessionId;

  /// The session entities that caused this suggestion. These are represented in
  /// terms of IDs of nodes in the session graph.
  Iterable<NodeId> get sourceEntities;

  /// Returns true, if the side-effects of applying this suggestion should be
  /// performed as part of a new session.
  bool get createsNewSession;

  /// Returns the [BasicEmbodiment] that can be used to construct UI for the
  /// suggestion in the case where more dynamic UI cannot be constructed (e.g. a
  /// module does not support the 'suggestion' display embodiment).
  BasicEmbodiment get basicEmbodiment;

  /// Returns the Manifest of the module to run to dynamically display this
  /// suggestion. The returned manifest must declare 'suggestion' as one of its
  /// display embodiments.
  Manifest get displayModule;

  bool get canDisplayLive => displayModule != null;

  // TODO(armansito): Remove these, as it doesn't make sense for a Suggestion to
  // self-assign relevance. I kept this here for now so that the Lasagna demo
  // doesn't break. Once we have a proper ranker, we should use |sourceEntities|
  // and other data to rank suggestions.
  //
  // Returns a score that represents this suggestions relevance given the
  // current context. The score value is a floating point number in the range
  // 0...INT_MAX where a higher score implies higher relevance.
  double get relevanceScore;
  int compareRelevance(final Suggestion other);

  /// Applies the action that this [Suggestion] represents.
  /// [sessionStateManager] can be used to interact with the session handler to
  /// make the necessary updates. Returns the ID of the session that this
  /// suggestion modified, or the ID of the new session if one was created as a
  /// result of applying this suggestion.
  Future<Uuid> apply(final SessionStateManager sessionStateManager);

  Map<String, dynamic> toJson() => {'id': id, 'sessionId': sessionId};
}

/// The basic set of data needed to present this suggestion to the user.
class BasicEmbodiment {
  final String description;
  final Uri iconUri;
  final int themeColor;

  BasicEmbodiment({this.description, this.iconUri, this.themeColor});
}
