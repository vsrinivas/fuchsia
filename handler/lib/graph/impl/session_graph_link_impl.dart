// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:async';
import 'dart:convert';
import 'dart:typed_data';

import 'package:collection/collection.dart';
import 'package:modular_core/uuid.dart' show Uuid;
import 'package:modular_core/graph/graph.dart';
import 'package:modular_core/graph/query/query.dart';
import 'package:modular_core/util/timeline_helper.dart';

import '../../constants.dart';
import '../session_graph_link.dart';
import 'graph_util.dart';
import 'session_graph_impl.dart';

/// [SessionGraphLinkImpl] manages the link specificed by a [SessionGraphLink].
class SessionGraphLinkImpl {
  final SerializableSessionGraphLink link;
  final SessionGraphImpl owner;

  // See linkedGraph getter, below.
  Graph _linkedGraph;
  Future<Graph> _linkedGraphFuture;

  Set<Node> _exposedNodes = new Set<Node>();
  Set<Edge> _exposedEdges = new Set<Edge>();
  Set<EdgeId> _linkEdgeIds = new Set<EdgeId>();

  SessionGraphLinkImpl(this.link, this.owner);

  Graph get linkedGraph => _linkedGraph;

  void start() {
    if (_linkedGraphFuture != null) {
      // Already started (or in the process of starting).
      return;
    }

    Future<Graph> thisFuture = _linkedGraphFuture =
        new Future<Graph>.value(owner.graphStore.findGraph(link.sessionId));
    thisFuture.then((Graph linkedGraph) {
      if (!identical(thisFuture, _linkedGraphFuture)) {
        // The link was stopped, so there is nothing more to do (even if it was
        // subsequently restarted, it's not our problem).
        return;
      }
      assert(_linkedGraph == null && linkedGraph != null);
      _linkedGraph = linkedGraph;

      _onGraphEvent(createGraphEventWithPlausibleHistoryOf(_linkedGraph));
      _linkedGraph.addObserver(_onGraphEvent);
    });
  }

  void stop() {
    if (_linkedGraph != null) {
      _linkedGraph.removeObserver(_onGraphEvent);

      // Remove all nodes/edges that were exposed to the SessionGraph.
      final Set<Node> removedNodes = _exposedNodes;
      final Set<Edge> removedEdges = _exposedEdges;
      _exposedNodes = new Set<Node>();
      _exposedEdges = new Set<Edge>();

      // Remove all the link edges. This allows the lower-level operations
      // in _addRemoveEdgesNodes to succeed by making sure no nodes have
      // lingering incoming edges that weren't in _exposedEdges.
      owner.mutate((GraphMutator mutator) {
        for (EdgeId id in _linkEdgeIds) {
          mutator.removeEdge(id);
        }
        _addRemoveEdgesNodes(
            mutator, removedEdges, removedNodes, _exposedEdges, _exposedNodes);
      }, tag: owner);

      _linkEdgeIds.clear();

      // Do this last, otherwise calls to clearPersistor() will fail.
      _linkedGraph = null;
    }

    // Clearing this future results in a "no-op then"; see start().
    _linkedGraphFuture = null;
  }

  void _onGraphEvent(GraphEvent event) {
    traceSync('$runtimeType._onGraphEvent()', () {
      final Set<Node> exposedNodes = new Set<Node>();
      final Set<Edge> exposedEdges = new Set<Edge>();
      final Set<Node> matchRoots = new Set<Node>();

      if (link.query == null) {
        exposedNodes.addAll(_linkedGraph.nodes);
        exposedEdges.addAll(_linkedGraph.edges);
      } else {
        // Gather all nodes/edges that are matched by the query.
        for (GraphQueryMatch match
            in link.query.match(_linkedGraph, keepRootEdge: true)) {
          // TODO(jjosh): GraphQueryMatch is super-inconvenient for this use-case.
          assert(match.rootNode.inEdges.length == 1);
          final Node rootNode = match.rootNode.inEdges.first.origin;
          matchRoots.add(rootNode);
          exposedNodes.add(rootNode);
          exposedNodes.addAll(match.matchedNodes);
          exposedEdges.addAll(match.matchedEdges);
        }
      }

      // Determine which nodes/edges were added/removed since the last query.
      final Set<Node> removedNodes = _exposedNodes.difference(exposedNodes);
      final Set<Edge> removedEdges = _exposedEdges.difference(exposedEdges);
      final Set<Node> addedNodes = exposedNodes.difference(_exposedNodes);
      final Set<Edge> addedEdges = exposedEdges.difference(_exposedEdges);

      // Remember these changes for next time.
      _exposedNodes = exposedNodes;
      _exposedEdges = exposedEdges;

      // Remove any link edges that are no longer going to point to valid
      // nodes.
      List<EdgeId> invalidLinkEdgeIds = _linkEdgeIds
          .where((EdgeId id) => !exposedNodes.contains(owner.edge(id).target))
          .toList();
      owner.mutate((GraphMutator mutator) {
        for (EdgeId id in invalidLinkEdgeIds) {
          mutator.removeEdge(id);
          _linkEdgeIds.remove(id);
        }

        // Reflect the changes from _linkedGraph into the SessionGraph.
        _addRemoveEdgesNodes(
            mutator, removedEdges, removedNodes, addedEdges, addedNodes);
        // Generate new edges from the link-origin to the root node of each
        // new match.  These edges are NOT persisted.
        if (link.originNode != null) {
          for (Node node in matchRoots.intersection(addedNodes)) {
            // TODO(thatguy): This is an ephemeral edge, so it should never be
            // persisted -- however, we don't tell SessionGraphImpl that this
            // is the case specifically, yet it doesn't seem to matter. Tests
            // pass that specifically check for if this edge is persisted.
            _linkEdgeIds.add(
                mutator.addEdge(link.originNode.id, link.labels, node.id).id);
          }
        }

        // Reflect any representation-value mutations in exposed nodes.
        for (GraphMutation mutation in event.mutations) {
          switch (mutation.type) {
            case GraphMutationType.setValue:
              // We only care about changes to Nodes matched by the query.
              final Node node = owner.node(mutation.nodeId);
              if (_exposedNodes.contains(node)) {
                // TODO(jjosh): filter values that don't appear in the query?
                mutator.setValue(
                    mutation.nodeId, mutation.valueKey, mutation.newValue);
              }
              break;
            default:
            // We're only concerned with value mutations, because we do
            // everything else based on query matches.
          }
        }
      }, tag: owner);
    }); // traceSync
  }

  // The arguments represent changes in the linked graph.  Make the
  // corresponding changes to the SessionGraph that owns this SessionGraphLinkImpl.
  void _addRemoveEdgesNodes(GraphMutator mutator, Set<Edge> removedEdges,
      Set<Node> removedNodes, Set<Edge> addedEdges, Set<Node> addedNodes) {
    // Remove any edges that no longer appear in the query matches.
    // Note: we remove edges before nodes because otherwise we couldn't
    // assert(ownerEdge != null).
    for (Edge edge in removedEdges) {
      final Edge ownerEdge = owner.edge(edge.id);
      assert(ownerEdge != null);
      owner.clearEdgePersistor(ownerEdge.id, _linkedGraph);
      mutator.apply(new GraphMutation.removeEdge(
          edge.id, ownerEdge.origin.id, ownerEdge.target.id, ownerEdge.labels));
    }
    // Remove any nodes that no longer appear in the query matches.
    for (Node node in removedNodes) {
      final Node ownerNode = owner.node(node.id);
      assert(ownerNode != null);
      owner.clearNodePersistor(ownerNode.id, _linkedGraph);
      mutator.apply(new GraphMutation.removeNode(node.id));
    }
    // Add any nodes that did not appear in previous query matches.
    for (Node node in addedNodes) {
      if (owner.node(node.id) != null) {
        // The node could be already injected to the owner graph, if the
        // mutation was on the owner graph.
        assert(() {
          final Node ownerNode = owner.node(node.id);
          return node.valueKeys.every((final String key) {
            return const ListEquality<int>()
                .equals(ownerNode.getValue(key), node.getValue(key));
          });
        });
        continue;
      }

      owner.setNodePersistor(node.id, _linkedGraph);
      mutator.apply(new GraphMutation.addNode(node.id));
      // We must explicitly set values because we cannot rely on mutations
      // to notify us. This is because the values may have been set on the
      // nodes before they became part of a QueryMatch.
      for (String key in node.valueKeys) {
        mutator.apply(
            new GraphMutation.setValue(node.id, key, node.getValue(key)));
      }
    }
    // Add any edges that did not appear in previous query matches.
    // Note: we add edges after nodes because otherwise we might not find the
    // necessary nodes.
    for (Edge edge in addedEdges) {
      if (owner.edge(edge.id) != null) {
        // The edge could be already injected to the owner graph, if the
        // mutation was on the owner graph.
        assert(() {
          final Edge ownerEdge = owner.edge(edge.id);
          return ownerEdge.origin.id == edge.origin.id &&
              ownerEdge.target.id == edge.target.id;
        });
        continue;
      }
      owner.setEdgePersistor(edge.id, _linkedGraph);
      mutator.apply(new GraphMutation.addEdge(
          edge.id, edge.origin.id, edge.target.id, edge.labels));
    }
  }
}

/// [SerializableSessionGraphLink] implements the [SessionGraphLink] interface,
/// and knows how to serialize itself to a [Graph].
class SerializableSessionGraphLink implements SessionGraphLink {
  /// Construct a new [SessionGraphLink] that can be passed to
  /// [SessionGraph.addLink] and [SessionGraph.removeLink].
  SerializableSessionGraphLink(Uuid sessionId, GraphQuery query,
      Node originNode, Iterable<String> labels)
      : this._(new Uuid.random(), sessionId, query, originNode,
            labels == null ? new Set<String>() : labels.toSet());

  /// Private constructor called by [SessionGraphLink.fromGraphMetadata].  This
  /// is the only way to construct a [SessionGraphLink]  with a particular
  /// [linkId], instead of a random one.
  SerializableSessionGraphLink._(
      this.linkId, this.sessionId, this.query, this.originNode, this._labels);

  @override // SessionGraphLink
  final Uuid linkId;
  @override // SessionGraphLink
  final Uuid sessionId;
  @override // SessionGraphLink
  final GraphQuery query;
  @override // SessionGraphLink
  final Node originNode;
  @override // SessionGraphLink
  Iterable<String> get labels => _labels;
  final Set<String> _labels;

  /// These static fields are used when serializing/deserializing a
  /// [SessionGraphLink] to/from a [Graph].
  static const String _linkLabel = '${Constants.sessionGraphLabelPrefix}link';
  static const String _linkMetadataKey =
      '${Constants.sessionGraphLabelPrefix}link_metadata';
  static const String _linkOriginLabel =
      '${Constants.sessionGraphLabelPrefix}link_origin';
  static GraphQuery _metadataExpr; // See _buildMetadataExpr().

  /// Apply a [metadataExpr] to find all of the [SessionGraphLinks] that have
  /// been serialized to the [Graph].  Use the matched nodes/edges to
  /// reconstruct a list of [SessionGraphLink].
  static Set<SessionGraphLink> fromGraphMetadata(Graph graph) {
    _metadataExpr = _metadataExpr ?? _buildMetadataExpr();

    final List<GraphQueryMatch> matches =
        _metadataExpr.match(graph, keepRootEdge: true);

    return matches.map((final GraphQueryMatch match) {
      final Iterable<Edge> linkEdges =
          match.matchedEdges.where((Edge e) => e.labels.contains(_linkLabel));
      assert(linkEdges.length == 1);

      final Node metadataNode = linkEdges.first.target;
      final Node originNode =
          metadataNode.singleOutEdgeWithLabels([_linkOriginLabel])?.target;

      final Uint8List encodedMetadata = metadataNode.getValue(_linkMetadataKey);
      assert(encodedMetadata != null);
      Map<String, dynamic> metadata = JSON.decode(UTF8.decode(encodedMetadata));
      assert(
          metadata['type'] == 'SessionGraphLink' && metadata['version'] == 1);
      metadata = metadata['data'];

      final GraphQuery query = metadata['query'] != null
          ? new GraphQuery.fromJSON(metadata['query'])
          : null;
      return new SerializableSessionGraphLink._(
          Uuid.fromBase64(metadata['linkId']),
          Uuid.fromBase64(metadata['sessionId']),
          query,
          originNode,
          metadata['labels'].toSet());
    }).toSet();
  }

  /// Return true if this link may conflict with the other one.
  bool mayConflictWith(SessionGraphLink other) {
    // TODO(jjosh): multiple links to the same session are prohibited because
    // the current implementation will not behave well if the same node/edge is
    // exposed multiple times.  The restriction on sessionId equality can be
    // relaxed when this is fixed.
    // TODO(jjosh): the restriction on sessionId is not sufficient to guarantee
    // that the same node/edge can't be exposed multiple times.
    // the following scenario:
    //   - session A links to sessions B and C.
    //   - sessions B and C both link to session D.
    return linkId == other.linkId || sessionId == other.sessionId;
  }

  void writeGraphMetadata(Graph graph, Node rootNode) {
    graph.mutate((GraphMutator mutator) {
      final Node linkNode = mutator.addNode();
      mutator.setValue(
          linkNode.id, _linkMetadataKey, _encodedRepresentationValue());
      if (originNode != null) {
        mutator.addEdge(linkNode.id, [_linkOriginLabel], originNode.id);
      }
      mutator.addEdge(rootNode.id, [_linkLabel], linkNode.id);
    });
  }

  void eraseGraphMetadata(Graph graph, Node rootNode) {
    final Uint8List metadata = _encodedRepresentationValue();
    final Edge link = rootNode.outEdges.firstWhere((Edge e) =>
        const ListEquality<int>()
            .equals(metadata, e.target.getValue(_linkMetadataKey)));

    graph.mutate((GraphMutator mutator) {
      mutator.removeNode(link.target.id);
    });
  }

  Uint8List _encodedRepresentationValue() {
    return new Uint8List.fromList(UTF8.encode(JSON.encode({
      'type': 'SessionGraphLink',
      'version': 1,
      'data': {
        'linkId': linkId.toBase64(),
        'sessionId': sessionId.toBase64(),
        'query': query != null ? JSON.decode(query.toJSON()) : null,
        'labels': labels.toList()
      }
    })));
  }

  @override // Object
  int get hashCode => linkId.hashCode;

  @override // Object
  bool operator ==(dynamic other) {
    if (other is SessionGraphLink && linkId == other.linkId) {
      // We compare only linkId for efficiency, but assert the rest for sanity.
      assert(sessionId == other.sessionId &&
          query == other.query &&
          originNode == other.originNode &&
          const SetEquality<String>().equals(_labels, other.labels));
      return true;
    }
    return false;
  }

  /// Build the [GraphQuery] that is used to search a [Graph] for metadata
  /// that represents a [SessionGraphLink].
  static GraphQuery _buildMetadataExpr() => new GraphQuery([
        _linkLabel
      ], valueLabels: [
        _linkMetadataKey
      ], childConstraints: [
        new GraphQuery([_linkOriginLabel], isRequired: false)
      ]);
}
