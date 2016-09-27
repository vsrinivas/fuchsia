// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:collection/collection.dart';

import '../../util/hash.dart';
import '../../util/timeline_helper.dart';
import '../graph.dart';
import 'query.dart';

/// Contains metadata about, and the matching [Node]s and [Edge]s when a
/// specific [GraphQuery] is matched against a specific [Graph] instance. A
/// single [GraphQuery] can result in multiple [GraphQueryMatch]
/// instances being returned (see doc for [GraphQueryMatcher]).
abstract class GraphQueryMatch {
  /// The node which matched the highest level "root" component of the
  /// [GraphQuery]. All [Node]s and [Edge]s in [matchedNodes] and
  /// [matchedEdges] have [rootNode] as a common ancestor.
  ///
  /// In the case of a non-repeating 'top-level' [GraphQuery], this returns
  /// the single root [Node]. In the case of a repeating query, returns null.
  Node get rootNode;

  /// Returns a list of root [Node]s in this match.
  Iterable<Node> get rootNodes;

  Set<Node> get matchedNodes;
  Set<Edge> get matchedEdges;

  /// Add an observer to this match. The observer implementation is
  /// shared with the [Graph] API and uses the same format.
  ///
  /// Adding an observer has the side-effect that this match will keep itself
  /// up-to-date when the structure of the underlying [Graph] changes. This
  /// means that [matchedNodes] and [matchedEdges] will change, but [rootNode]
  /// will never change -- ie, the match will remain 'anchored' at the same
  /// point in the [Graph].
  ///
  /// If [rootNode] ends up being deleted from the [Graph], [matchedNodes]
  /// and [matchedEdges] will be empty. No further events will be propagated,
  /// unless a [Node] with the same ID as the original [rootNode] is added again
  /// and happens to match the original query.
  ///
  /// Events are filtered to pertain only to those nodes present in
  /// [matchedNodes] and [matchedEdges], and are otherwise discarded.
  void addObserver(final GraphChangeCallback observer);

  /// Remove a previously-added observer from this match. If this is the last
  /// observer, the match will no longer keep itself up-to-date with changes
  /// to the underlying structure of the matched [Graph].
  void removeObserver(final GraphChangeCallback observer);

  // Returns a copy of this match without observers.
  GraphQueryMatch clone();
}

/// Performs the matching process on a given [Graph] for any [GraphQuery]
/// instance. Depending on the [GraphQuery] and the structure of the stored
/// [Node]s and [Edge]s in the [Graph], zero or more [GraphQueryMatch]
/// instances are generated.
class GraphQueryMatcher {
  final Graph _graph;

  GraphQueryMatcher(this._graph);

  /// Returns zero or more [GraphQueryMatch] objects created by looking for
  /// sets of [Node]s and [Edge]s which satisfy the constraints in [query].
  /// The number of matches returned depends on the constraints set forth in
  /// [query], and represents each unique way that the contraints can be
  /// satisfied in the [Graph] passed into the constructor.
  List<GraphQueryMatch> match(final GraphQuery query,
      {bool keepRootEdge: false}) {
    return traceSync('$runtimeType.match()', () {
      assert(query.validate());

      final List<GraphQueryMatch> matches = new List<GraphQueryMatch>();
      for (Node node in _graph.nodes) {
        matches.addAll(_matchNode(node, query, isRoot: !keepRootEdge));
      }

      return matches;
    }); // traceSync
  }

  List<GraphQueryMatch> matchNode(Node node, GraphQuery query) =>
      traceSync('$runtimeType.matchNode()', () {
        return _matchNode(node, query, isRoot: true);
      });

  List<GraphQueryMatchImpl> _matchNode(Node node, GraphQuery originalQuery,
      {bool isRoot: false}) {
    return traceSync('$runtimeType._matchNode()', () {
      GraphQuery query = originalQuery;
      if (isRoot && originalQuery.isRepeated) {
        // In the case of a repeated root query, we re-write it to a single
        // root node query with a wildcard, and then we strip out the root node
        // and its edges once the match is done.
        query = new GraphQuery([], childConstraints: [originalQuery]);
      }
      GraphQueryMatchImpl baseMatch =
          new GraphQueryMatchImpl._([node], _graph, query);
      baseMatch.matchedNodes.add(node);

      if (!query.isWildcard) {
        // Look for in-edges that match any of the labels in [query.inEdgeLabels].
        final List<Edge> edges = _matchInEdgeLabels(query.inEdgeLabels, node);
        if (edges.isEmpty) return <GraphQueryMatchImpl>[];
        if (!isRoot) baseMatch.matchedEdges.addAll(edges);
      }

      if (query.valueLabels.isNotEmpty) {
        // This [Node] must have values for at least one the value labels that
        // listed.
        List<String> valueKeys = new List<String>.from(node.valueKeys);
        if (query.valueLabels.every((String l) => !valueKeys.contains(l))) {
          return <GraphQueryMatchImpl>[];
        }
      }

      // If we don't have any [childConstraints], we're done!
      if (query.childConstraints.isEmpty) return [baseMatch];

      // We go through each [childConstraint], and store all the matches we get
      // from each. We then take the cartesian-product of all of them as our final
      // match set for this node.
      List<List<GraphQueryMatch>> childMatches =
          new List<List<GraphQueryMatchImpl>>();
      for (GraphQuery childquery in query.childConstraints) {
        List<GraphQueryMatchImpl> thisChildMatches =
            new List<GraphQueryMatchImpl>();
        for (Edge edge in node.outEdges) {
          thisChildMatches.addAll(_matchNode(edge.target, childquery));
        }
        // If there were no matches, then either we fail (because the match was
        // required), or we are incomplete.
        if (thisChildMatches.isEmpty) {
          if (childquery.isRequired) return <GraphQueryMatchImpl>[];
          continue; // Don't bother adding the empty list to [childMatches].
        }
        // If this child is repeated, merge all the matches into one. Otherwise,
        // just add them all.
        if (childquery.isRepeated) {
          for (int i = 1; i < thisChildMatches.length; i++) {
            thisChildMatches[0]._mergeFrom(thisChildMatches[i]);
          }
          childMatches.add([thisChildMatches[0]]);
        } else {
          childMatches.add(thisChildMatches);
        }
      }

      GraphQueryMatch _maybeStripRoot(final GraphQueryMatch match) {
        if (!isRoot || !originalQuery.isRepeated) return match;

        // Find the new roots, which are all going to be children of the
        // match's root node.
        final Set<Node> rootChildren =
            match.rootNode.outEdges.map((final Edge e) => e.target).toSet();
        final Set<Node> newRoots =
            rootChildren.intersection(new Set<Node>.from(match.matchedNodes));
        final GraphQueryMatchImpl newMatch =
            new GraphQueryMatchImpl._(newRoots.toList(), _graph, originalQuery);

        newMatch.matchedNodes.addAll(match.matchedNodes);
        newMatch.matchedNodes.remove(match.rootNode);

        newMatch.matchedEdges.addAll(
            match.matchedEdges.difference(match.rootNode.outEdges.toSet()));

        return newMatch;
      }

      if (childMatches.isEmpty) {
        // There were no matches from children, but also none of them were
        // required (that was handled above), so just return our [baseMatch].
        return [_maybeStripRoot(baseMatch)];
      }

      return _cartesianProduct(baseMatch, childMatches)
          .map(_maybeStripRoot)
          .toList();
    }); // traceSync
  }

  // Returns a list of in-edges on [node] that have all the labels in
  // [labels].
  List<Edge> _matchInEdgeLabels(Set<String> matchLabels, Node node) {
    final List<Edge> edges = new List<Edge>();
    edges.addAll(node.inEdges
        .where((Edge edge) => edge.labels.toSet().containsAll(matchLabels)));
    return edges;
  }

  List<GraphQueryMatchImpl> _cartesianProduct(
      GraphQueryMatchImpl baseMatch, List<List<GraphQueryMatchImpl>> input) {
    return input.fold([baseMatch], (final List<GraphQueryMatchImpl> matches1,
        final List<GraphQueryMatchImpl> matches2) {
      final List<GraphQueryMatchImpl> result = <GraphQueryMatchImpl>[];
      for (final GraphQueryMatchImpl m1 in matches1) {
        for (final GraphQueryMatchImpl m2 in matches2) {
          GraphQueryMatchImpl merged = m1.clone();
          merged._mergeFrom(m2);
          result.add(merged);
        }
      }
      return result;
    });
  }
}

class GraphQueryMatchImpl implements GraphQueryMatch {
  @override
  final Node rootNode;
  @override
  final List<Node> rootNodes;

  @override
  final Set<Node> matchedNodes = new Set<Node>();
  @override
  final Set<Edge> matchedEdges = new Set<Edge>();

  final Graph _graph;
  final GraphQuery _query;
  final List<GraphChangeCallback> _observers = <GraphChangeCallback>[];

  GraphQueryMatchImpl._(List<Node> roots, this._graph, final GraphQuery query)
      : rootNode = query.isRepeated ? null : roots[0],
        rootNodes = roots,
        _query = query;
  @override
  GraphQueryMatch clone() {
    GraphQueryMatchImpl m =
        new GraphQueryMatchImpl._(rootNodes, _graph, _query);
    m._mergeFrom(this);
    return m;
  }

  @override
  void addObserver(final GraphChangeCallback observer) {
    if (_observers.isEmpty) {
      _graph.addObserver(_onGraphChange);
    }
    _observers.add(observer);
  }

  @override
  void removeObserver(final GraphChangeCallback observer) {
    _observers.remove(observer);
    if (_observers.isEmpty) {
      _graph.removeObserver(_onGraphChange);
    }
  }

  void _onGraphChange(final GraphEvent event) {
    // Naively re-evaluate our new matchset.
    matchedNodes.clear();
    matchedEdges.clear();

    // Make sure that our rootNode is still in the graph before re-issuing
    // the query.
    if (!rootNode.isDeleted) {
      final List<GraphQueryMatch> newMatches =
          new GraphQueryMatcher(_graph).matchNode(rootNode, _query);
      if (newMatches.isNotEmpty) {
        // TODO(thatguy): As long as queries on a single node can produce
        // multiple matches, this will always cause updates to a match that
        // share a query and a rootNode to show the exact same matched nodes,
        // regardless of the combination of nodes at the time of construction.
        //
        // We should probably just enforce that [matchNode()] either returns
        // one or zero matches, and get rid of the cartesian product during
        // matching.
        _mergeFrom(newMatches[0]);
      }
    }

    final Set<NodeId> newNodeIds = matchedNodes.map((Node n) => n.id).toSet();
    final Set<EdgeId> newEdgeIds = matchedEdges.map((Edge e) => e.id).toSet();

    // Filter event.mutations to include only what's in our match.
    List<GraphMutation> newMutations =
        event.mutations.where((GraphMutation mutation) {
      switch (mutation.type) {
        case GraphMutationType.addEdge:
        case GraphMutationType.removeEdge:
          return newEdgeIds.contains(mutation.edgeId);
        case GraphMutationType.addNode:
        case GraphMutationType.removeNode:
        case GraphMutationType.setValue:
          return newNodeIds.contains(mutation.nodeId);
      }
    }).toList();
    if (newMutations.isEmpty) return;

    final GraphEvent newEvent = new GraphEvent(event.graph, newMutations);
    _notifyObservers(newEvent);
  }

  void _notifyObservers(final GraphEvent event) {
    _observers.forEach((GraphChangeCallback observer) {
      observer(event);
    });
  }

  void _mergeFrom(GraphQueryMatchImpl other) {
    matchedNodes.addAll(other.matchedNodes);
    matchedEdges.addAll(other.matchedEdges);
  }

  @override
  int get hashCode =>
      hash3(rootNode.hashCode, matchedNodes.hashCode, matchedEdges.hashCode);

  @override
  bool operator ==(Object other) {
    return other is GraphQueryMatch &&
        identical(rootNode, other.rootNode) &&
        const SetEquality<Node>().equals(matchedNodes, other.matchedNodes) &&
        const SetEquality<Edge>().equals(matchedEdges, other.matchedEdges);
  }
}
