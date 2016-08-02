// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:modular_core/graph/mem_graph.dart';
import 'package:modular_core/graph/mutation.dart';
import 'package:modular_core/graph/mixins/querying_mixin.dart';
import 'package:modular_core/graph/query/query.dart';
import 'package:modular_core/graph/validating_lazy_cloned_graph_state.dart';
import 'package:modular_core/log.dart';

import '../../constants.dart';
import '../session_graph.dart';
import '../session_graph_store.dart';
import 'graph_util.dart';
import 'session_graph_link_impl.dart';

/// Wraps a [GraphChangeCallback] so that each [GraphEvent] passed to it appears
/// to originate in a different [Graph].
class _GraphChangeCallbackWrapper {
  final Graph _graph;
  final GraphChangeCallback _originalCallback;
  _GraphChangeCallbackWrapper(this._graph, this._originalCallback);
  void call(GraphEvent event) {
    _originalCallback(new GraphEvent(_graph, event.mutations));
  }

  @override
  bool operator ==(other) =>
      other is _GraphChangeCallbackWrapper &&
      _originalCallback == other._originalCallback;

  @override
  int get hashCode => _originalCallback.hashCode;
}

/// [SessionGraphImpl] is the sole implementor of [SessionGraph].
class SessionGraphImpl extends SessionGraph with GraphQueryingMixin {
  // Underlying graph.  Typically, this would be synced to the ledger.
  final Graph baseGraph;

  // ID of the Session that owns this graph.
  final Uuid sessionId;

  // Contains other graphs that are linked into the session.  There should
  // be no internal nodes/edges visible in any of these graphs.
  final Map<SessionGraphLink, SessionGraphLinkImpl> _links =
      <SessionGraphLink, SessionGraphLinkImpl>{};

  final MemGraph _reflectedGraph;

  // Used to obtain linked graphs based on their SessionId.
  final SessionGraphStore graphStore;

  // Used internally, and by SessionGraphLinkImpl.
  final Edge metadataEdge;

  SessionGraphImpl(Uuid sessionId, this.baseGraph, this.metadataEdge,
      this.graphStore, String prefix)
      : sessionId = sessionId,
        _reflectedGraph = new MemGraph(prefix: prefix) {
    assert(sessionId != null || baseGraph != null && graphStore != null);
    assert(metadataEdge == baseGraph.edge(metadataEdge.id));
    assert(prefix != null);
    metadata.debugName = 'SessionGraphImpl';
    metadata.debugTraceId = sessionId.toString();

    _reflectedGraph.addObserver(_onReflectedGraphEvent);
    _reflectedGraph.metadata.debugName = 'SessionGraphImpl._reflectedGraph';
    _reflectedGraph.metadata.debugTraceId = sessionId.toString();

    _onBaseGraphEvent(createGraphEventWithPlausibleHistoryOf(baseGraph));

    _rootNode = _reflectedGraph.node(metadataEdge.origin.id);
    _metadataNode = _reflectedGraph.node(metadataEdge.target.id);

    baseGraph.addObserver(_onBaseGraphEvent);
  }

  @override // For mixins.
  Graph get mixinGraph => this;

  @override
  NodeIdGenerator get nodeIdGenerator => _reflectedGraph.nodeIdGenerator;
  @override
  EdgeIdGenerator get edgeIdGenerator => _reflectedGraph.edgeIdGenerator;

  @override
  GraphState get state => _reflectedGraph.state;

  // Supports iteratation over all links that have been 'established', i.e.
  // which have obtained the corresponding session graph from the graph store.
  Iterable<SessionGraphLinkImpl> get establishedLinks => _links.values
      .where((SessionGraphLinkImpl impl) => impl.linkedGraph != null);

  List<Graph> get allLinkedGraphs => establishedLinks
      .map((SessionGraphLinkImpl link) => link.linkedGraph)
      .toList();

  List<Graph> get allSourceGraphs => allLinkedGraphs..add(baseGraph);

  @override
  Iterable<SessionGraphLink> get links => _links.keys;

  // Reflected version of root node of 'baseGraph'.
  @override // SessionGraph
  Node get root => _rootNode;
  Node _rootNode;

  // Reflected version of metadata node for this session.
  @override // SessionGraph
  Node get metadataNode => _metadataNode;
  Node _metadataNode;

  final Logger _log = log('handler.SessionGraph');

  @override // Graph
  void addObserver(GraphChangeCallback callback) {
    _reflectedGraph
        .addObserver(new _GraphChangeCallbackWrapper(this, callback));
  }

  @override // Graph
  void removeObserver(GraphChangeCallback callback) {
    _reflectedGraph
        .removeObserver(new _GraphChangeCallbackWrapper(this, callback));
  }

  @override // Graph
  Iterable<Node> get nodes => _reflectedGraph.nodes;

  @override // Graph
  Iterable<Edge> get edges => _reflectedGraph.edges;

  @override // Graph
  Node node(NodeId nodeId) => _reflectedGraph.node(nodeId);

  @override // Graph
  Edge edge(EdgeId edgeId) => _reflectedGraph.edge(edgeId);

  @override // Graph
  void mutate(MutateGraphCallback fn, {dynamic tag}) {
    _reflectedGraph.mutate(fn, tag: tag);
  }

  @override // SessionGraph
  SessionGraphLink addSessionLink(Uuid sessionId,
      {GraphQuery query,
      Node linkOrigin,
      Iterable<String> labels: const <String>[]}) {
    // If not null, 'linkOrigin' must be a node exposed by this SessionGraph.
    // Replace it with the corresponding node from 'baseGraph'.
    if (linkOrigin != null) {
      assert(linkOrigin == node(linkOrigin.id));
      linkOrigin = baseGraph.node(linkOrigin.id);
      assert(linkOrigin != null);
    }
    final SerializableSessionGraphLink link =
        new SerializableSessionGraphLink(sessionId, query, linkOrigin, labels);
    for (final SessionGraphLink existingLink in links) {
      if (link == existingLink) {
        return link;
      } else if (link.mayConflictWith(existingLink)) {
        _log.severe('new session-link conflicts with existing session-link');
        _log.severe('    new: $link');
        _log.severe('    existing: $existingLink');
        return null;
      }
    }
    link.writeGraphMetadata(baseGraph, metadataEdge.target);

    // No other setup is required after writing the link metadata to the graph.
    // Instead, we react to graph mutations, which is necessary anyway.  For
    // example, if addSessionLink() is called on a replica of this SessionGraph,
    // then we react to the synced changes exactly like that replica does.
    assert(links.contains(link));
    return link;
  }

  @override // SessionGraph
  void removeSessionLink(SessionGraphLink link) {
    if (links.contains(link) && link is SerializableSessionGraphLink) {
      link.eraseGraphMetadata(baseGraph, metadataEdge.target);
      assert(!links.contains(link));
    } else {
      _log.warning('Could not find link to remove: ${link.linkId}');
    }
  }

  Map<EdgeId, GraphMutation> _unpersistedAddEdgeMutations =
      <EdgeId, GraphMutation>{};
  Map<NodeId, List<GraphMutation>> _unpersistedNodeMutations =
      <NodeId, List<GraphMutation>>{};

  void _maybePersistEdge(
      Edge edgeRef, Map<Graph, List<GraphMutation>> mutationsToForward) {
    // All the display edges are treated ephemerally.
    // TODO(jjosh): This eventually should be handled more generally, perhaps by
    // exposing some notion of ephemerality in the Graph API, so that the caller
    // can set ephemerality while adding edge.
    if (isDisplayNode(edgeRef.target)) return;

    Graph persistor = getNodePersistor(edgeRef.origin.id);
    if (persistor == null) {
      // The origin node isn't persisted yet so there's nothing we should do at
      // this time.
      return;
    }

    // Helper method to recursively persist edges and their target node.
    void persistEdgeHelper(Edge edge, Graph persistor) {
      assert(!isEdgePersisted(edge.id) && persistor != null);

      mutationsToForward.putIfAbsent(persistor, () => <GraphMutation>[]);

      final Node target = edge.target;
      if (isNodePersisted(target.id)) {
        // Persisted edges cannot point to a target that is in a different
        // graph.  Perhaps this can be relaxed later.
        assert(identical(persistor, getNodePersistor(target.id)));
      } else {
        // Add the node to the mutations list.
        if (_unpersistedNodeMutations.containsKey(target.id)) {
          // If _unpersistedNodeMutations doesn't contain target.id any more,
          // it means we already saw it while traversing in this round of
          // _maybePersistEdge(), but it hasn't had a chance to have
          // setNodePersistor() called, since that only happens once the
          // applied GraphEvent is handled in _onBaseGraphEvent().
          mutationsToForward[persistor]
            ..addAll(_unpersistedNodeMutations[target.id]);
          _unpersistedNodeMutations.remove(target.id);
        }

        // Recursively persist any outgoing edges that aren't yet persisted.
        for (final Edge e in target.outEdges) {
          if (!isEdgePersisted(e.id)) {
            persistEdgeHelper(e, persistor);
          } else {
            assert(getEdgePersistor(e.id) == persistor);
          }
        }
      }

      assert(_unpersistedAddEdgeMutations.containsKey(edge.id));
      mutationsToForward[persistor].add(_unpersistedAddEdgeMutations[edge.id]);
      _unpersistedAddEdgeMutations.remove(edge.id);
    }
    persistEdgeHelper(edgeRef, persistor);
  }

  // Handles 'setValue' mutations, since MemGraphNode doesn't have a way
  // for us to intercept them directly.  Contrast this with e.g. removeEdge()
  // where we directly call the reflected node's persistor.
  //
  // Note: we intentionally don't filter out events with clientId == this.
  void _onReflectedGraphEvent(GraphEvent event) {
    // A map that keeps track of what mutations to send to what underlying
    // Graph.
    Map<Graph, List<GraphMutation>> mutationsToForward =
        <Graph, List<GraphMutation>>{};

    List<GraphMutation> edgesToMaybePersist = <GraphMutation>[];
    for (GraphMutation mutation in event.mutations.coalesced.withoutTag(this)) {
      Graph persistor;
      if (mutation.isNodeMutation) {
        persistor = getNodePersistor(mutation.nodeId);
      } else {
        // mutation.isEdgeMutation
        persistor = getEdgePersistor(mutation.edgeId);
      }
      if (persistor != null) {
        mutationsToForward.putIfAbsent(
            persistor, () => new List<GraphMutation>());
        mutationsToForward[persistor].add(mutation);
      } else {
        if (mutation.isNodeMutation) {
          _unpersistedNodeMutations.putIfAbsent(
              mutation.nodeId, () => new List<GraphMutation>());
          _unpersistedNodeMutations[mutation.nodeId].add(mutation);
        } else if (mutation.type == GraphMutationType.addEdge) {
          assert(!_unpersistedAddEdgeMutations.containsKey(mutation.edgeId));
          _unpersistedAddEdgeMutations[mutation.edgeId] = mutation;
        }
      }

      // Persistors are updated once we apply mutations to the base graph, and
      // see the results of it in _onBaseGraphEvent(), so we don't need to keep
      // persistors up-to-date here.

      // What *does* matter here is that if an edge was added, and it "connects"
      // a node that was not persisted to one that was, we now know where to
      // persist it and we recursively do this.
      if (mutation.type == GraphMutationType.addEdge &&
          !isEdgePersisted(mutation.edgeId)) {
        edgesToMaybePersist.add(mutation);
      }
    }

    for (GraphMutation mutation in edgesToMaybePersist) {
      _maybePersistEdge(
          _reflectedGraph.edge(mutation.edgeId), mutationsToForward);
    }

    for (Graph persistor in mutationsToForward.keys) {
      persistor.mutate((GraphMutator mutator) {
        mutationsToForward[persistor].forEach(mutator.apply);
      }, tag: this);
    }
  }

  void _onBaseGraphEvent(GraphEvent event) {
    // Keeps track of mutations that we are going to forward on to the reflected
    // Graph.
    List<GraphMutation> mutationsToForward = <GraphMutation>[];
    bool linksChanged = false;
    // Here we intentionally do not ignore mutations with tags.contains(this)
    // because we rely on this code to update persistors for nodes/edges that
    // are modified either directly by some other client, or through our
    // handling of changes in the reflected graph in _onReflectedGraphEvent().
    // TODO(jjosh): the ledger graph should ensure that mutations are coalesced.
    for (GraphMutation mutation in event.mutations.coalesced) {
      if (_processBaseGraphMutation(
          mutation, event.graph, mutationsToForward)) {
        linksChanged = true;
      }
    }

    final validator = new ValidatingLazyClonedGraphState.forGraph(
        _reflectedGraph,
        allowRedundantMutations: true);
    final List<GraphMutation> validatedMutations =
        mutationsToForward.where(validator.applyMutation).toList();

    _reflectedGraph.mutate((GraphMutator mutator) {
      new GraphMutationList.from(validatedMutations)
          .withoutTag(this)
          .forEach(mutator.apply);
    }, tag: this);

    if (linksChanged) {
      assert(event.graph == baseGraph);

      final Set<SessionGraphLink> oldLinks = _links.keys.toSet();
      final Set<SessionGraphLink> links =
          SerializableSessionGraphLink.fromGraphMetadata(baseGraph);

      for (final SessionGraphLink removed in oldLinks.difference(links)) {
        _links[removed].stop();
        _links.remove(removed);
      }

      for (final SessionGraphLink added in links.difference(oldLinks)) {
        final SessionGraphLinkImpl link = new SessionGraphLinkImpl(added, this);
        _links[added] = link;
        link.start();
      }
    }
  }

  // Reflect changes in baseGraph so that they are visible in the SessionGraph.
  // Return true if the mutation may indicate a change in the configuration of
  // links to other sessions.
  bool _processBaseGraphMutation(GraphMutation mutation, Graph graph,
      List<GraphMutation> mutationsToForward) {
    // Predicate that returns true if any of the labels begin with
    // 'internal:session_graph:'.
    bool _isInternal(Iterable<String> labels) {
      return labels.any((String label) =>
          label.startsWith(Constants.sessionGraphLabelPrefix));
    }

    switch (mutation.type) {
      case GraphMutationType.addNode:
        setNodePersistor(mutation.nodeId, graph);
        break;
      case GraphMutationType.removeNode:
        final Node node = _reflectedGraph.node(mutation.nodeId);
        if (node != null) {
          clearNodePersistor(node.id, graph);
          break;
        }
        // If the node wasn't found, this must be because it was masked due to
        // its 'internal' values.  Unfortunately, we have no way to verify this,
        // so we simply assume that the configuration of links  has changed.
        return true;
      case GraphMutationType.addEdge:
        if (_isInternal(mutation.labels)) {
          return true;
        }
        setEdgePersistor(mutation.edgeId, graph);
        break;
      case GraphMutationType.removeEdge:
        if (_isInternal(mutation.labels)) {
          assert(edge(mutation.edgeId) == null);
          return true;
        }
        // final Edge edge = edge(mutation.edgeId);
        // assert(edge != null && isEdgePersisted(edge.id));
        clearEdgePersistor(mutation.edgeId, graph);
        break;
      case GraphMutationType.setValue:
        if (_isInternal([mutation.valueKey])) {
          return true;
        }
        break;
    }
    mutationsToForward.add(mutation);
    return false;
  }

  // Nodes may be persisted at most one of the source graphs that are exposed
  // via the session graph.  Keep track of which graph stores each node.
  final Map<NodeId, Graph> _nodePersistors = <NodeId, Graph>{};

  // Edges may be persisted at most one of the source graphs that are exposed
  // via the session graph.  Keep track of which graph stores each edge.
  final Map<EdgeId, Graph> _edgePersistors = <EdgeId, Graph>{};

  // Return the graph that persists the specified node, or null.
  Graph getNodePersistor(NodeId id) {
    return _nodePersistors[id];
  }

  // Return the graph that persists the specified edge, or null.
  Graph getEdgePersistor(EdgeId id) {
    return _edgePersistors[id];
  }

  // Return true if the node is currently persisted in one of the source graphs.
  bool isNodePersisted(NodeId id) => getNodePersistor(id) != null;

  // Return true if the node is currently persisted in one of the source graphs.
  bool isEdgePersisted(EdgeId id) => getEdgePersistor(id) != null;

  // Clear the graph that is responsible for persisting the node.
  void clearNodePersistor(NodeId id, Graph currentPersistor) {
    assert(_reflectedGraph.node(id) != null);
    final Graph result = _nodePersistors.remove(id);
    assert(identical(result, currentPersistor));
  }

  // Clear the graph that is responsible for persisting the edge.
  void clearEdgePersistor(EdgeId id, Graph currentPersistor) {
    final Graph result = _edgePersistors.remove(id);
    assert(identical(result, currentPersistor));
  }

  // Set the graph that is responsible for persisting the node.
  void setNodePersistor(NodeId id, Graph persistor) {
    assert(!isNodePersisted(id) || persistor == null);
    _nodePersistors[id] = persistor;
  }

  // Set the graph that is responsible for persisting the edge.
  void setEdgePersistor(EdgeId id, Graph persistor) {
    assert(!isEdgePersisted(id) || persistor == null);
    _edgePersistors[id] = persistor;
  }

  // Return the corresponding node in the graph that persists the node.
  Node getPersistedNode(NodeId id) {
    final Node node = getNodePersistor(id)?.node(id);
    assert(node != null);
    return node;
  }

  // Return the corresponding edge in the graph that persists the edge.
  Edge getPersistedEdge(EdgeId id) {
    final Edge edge = getEdgePersistor(id)?.edge(id);
    assert(edge != null);
    return edge;
  }
}
