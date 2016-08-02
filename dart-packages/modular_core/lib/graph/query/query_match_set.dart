// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:collection';

import 'package:collection/collection.dart';

import '../graph.dart';
import 'query.dart';
import 'query_match.dart';

typedef void VoidCallback();

/// Encapsulates a number of [GraphQueryMatch] objects that match a specific
/// [GraphQuery]. Can optionally be observed for changes to the resulting
/// matches.
abstract class GraphQueryMatchSet extends IterableBase<GraphQueryMatch> {
  @override
  Iterator<GraphQueryMatch> get iterator;

  GraphQueryMatch operator [](int i);

  /// If an observer is added, the observer will be notified if the set of
  /// matches in the underlying [Graph] for a given [GraphQuery] changes.
  ///
  /// A change is consider to have happened if a [Node] exists where previously
  /// a match did not occur, and now a match occurs starting from that [Node],
  /// or vice versa (a [Node] no longer acts as the root of a match).
  ///
  /// Changes to graph structure that are isolated within an existing match (ie,
  /// a [Node] value is changed, or a repeated [GraphQuery] component matches
  /// more nodes) will not trigger a callback here. However,
  /// [GraphQueryMatch.addObserver()] can be used to be notified for these types
  /// of changes for a specific match.
  void addObserver(final VoidCallback observer);
  void removeObserver(final VoidCallback observer);
}

///////////////////////////////////////////////////////////////////////////////

class GraphQueryMatchSetImpl extends GraphQueryMatchSet {
  final Graph _graph;
  final GraphQuery _query;
  final List<GraphQueryMatch> _matches = <GraphQueryMatch>[];

  final List<VoidCallback> _observers = <VoidCallback>[];

  GraphQueryMatchSetImpl(this._graph, this._query) {
    _recomputeMatches();
  }

  @override
  Iterator<GraphQueryMatch> get iterator => _matches.iterator;

  @override
  GraphQueryMatch operator [](int i) => _matches[i];

  @override
  void addObserver(final VoidCallback observer) {
    if (_observers.isEmpty) {
      _graph.addObserver(_onGraphChange);
    }
    _observers.add(observer);
  }

  @override
  void removeObserver(final VoidCallback observer) {
    _observers.remove(observer);
    if (_observers.isEmpty) {
      _graph.removeObserver(_onGraphChange);
    }
  }

  void _recomputeMatches() {
    _matches.clear();
    _matches.addAll(new GraphQueryMatcher(_graph).match(_query));
  }

  void _onGraphChange(final GraphEvent event) {
    final Set<NodeId> previousMatchRoots =
        _matches.map((GraphQueryMatch match) => match.rootNode.id).toSet();
    _recomputeMatches();
    final Set<NodeId> newMatchRoots =
        _matches.map((GraphQueryMatch match) => match.rootNode.id).toSet();

    // If the set of root nodes changed, notify our observers.
    if (!const SetEquality<NodeId>()
        .equals(previousMatchRoots, newMatchRoots)) {
      _observers.forEach((VoidCallback observer) {
        observer();
      });
    }
  }
}
