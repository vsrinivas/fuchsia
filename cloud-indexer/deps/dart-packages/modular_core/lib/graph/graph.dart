// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:typed_data';

import '../uuid.dart';
import 'bound_ref.dart';
import 'id.dart';
import 'ref.dart';
import 'mutation.dart';
import 'mutation_list.dart';
import 'query/query.dart';
import 'query/query_match_set.dart';

export 'id.dart' show NodeId, EdgeId, NodeIdGenerator, EdgeIdGenerator;
export 'mutation.dart'
    show
        GraphMutationType,
        GraphMutation,
        GraphMutationList,
        GraphMutator,
        FailedGraphMutation;
export 'ref.dart' show Node, Edge;

typedef void GraphChangeCallback(GraphEvent event);
typedef void MutateGraphCallback(GraphMutator mutator);

/// A [Graph] is an observable, directed semantic graph.
///
/// * Nodes may contain any number of values, keyed by String, with a value of
///   bytes represented as a Uint8List.
/// * Edges may contain any number of labels, which allow some semantic meaning
///   to be associated between the edge's origin and target.
/// * Multiple edges between the same pair of nodes (aka "parallel" edges) are
///   allowed.
/// * Node and Edge instances are akin to weak references to real data
///   in a [Graph].
///
/// Implementations may want to use the abstract [GraphBase] as a base class.
abstract class Graph {
  GraphMetadata metadata = new GraphMetadata();

  NodeIdGenerator get nodeIdGenerator;
  EdgeIdGenerator get edgeIdGenerator;

  Iterable<Node> get nodes;
  Iterable<Edge> get edges;

  /// Returns null if no node with [id] exists.
  Node node(NodeId id);

  /// Returns null if no edge with [id] exists.
  Edge edge(EdgeId id);

  GraphQueryMatchSet query(GraphQuery query);

  /// Calls [fn] with a [GraphMutator] object, allowing the callback to make
  /// changes to the structure of this graph.
  ///
  /// By the time [mutate()] returns, the mutations performed on the graph
  /// will either all be applied to the graph state, or an exception will be
  /// thrown. Changes won't be visible on [this] until [mutate()] returns.
  ///
  /// One side-effect of mutating the graph is the creation of [GraphMutation]
  /// records, which are included in a [GraphEvent] that is pushed to observers
  /// only *after* [fn] has finished running, but before mutate() returns.
  /// A single call to mutate() will produce a single [GraphEvent].
  ///
  /// If the optional [tag] is provided, each [GraphMutation] record
  /// is annotated. These can be used to identify [GraphMutations]s that were
  /// produced as a result of changes made during the execution of [fn].
  ///
  /// Errors encountered during [mutate()] result in a [FailedGraphMutation]
  /// exception, and the Graph will be left in the state it was prior to
  /// [mutate()].
  void mutate(MutateGraphCallback fn, {dynamic tag});

  //////////////////////////////////////////////////////////////////////////
  /// Observation

  /// Adds [callback] as an observer on the graph. Observers are notified of
  /// mutation events through a [GraphEvent] object, which includes multiple
  /// [GraphMutation] records.
  void addObserver(GraphChangeCallback callback);

  /// Removes [callback] as an observer on the graph.
  void removeObserver(GraphChangeCallback callback);

  /// Provides a low-level read-only view of the Graph state. The returned
  /// object must remain the same for the lifetime of this Graph.
  GraphState get state;

  /// Create [Node] reference objects. Implementors may want to
  /// sub-class [BoundNodeRef] and return that instead.
  /// All [Node] creation should use this method. The returned [Node] is not
  /// guaranteed to be valid (ie, a call to [isDeleted] may return true
  /// immediately).
  NodeRefFactory get nodeRefFactory =>
      (NodeId id) => new GraphBoundNodeRef(this, id);

  @override
  String toString() => '$runtimeType: $metadata';

  dynamic toJson() => {
        'nodes': nodes.map((Node node) => node.toJson()).toList(),
        'edges': edges.map((Edge edge) => edge.toJson()).toList(),
      };
}

/// Provides a low-level and read-only view of a Graph's state. This is the
/// source of truth for any [Graph] interface to expose its state to clients,
/// although most clients will want to use the convenience methods exposed
/// through [Graph], [Node] and [Edge].
///
/// The state of the graph may change at any time. Clients should not assume
/// state to remain the same across asynchronous gaps. Similarly, the Iterables
/// returned are only guaranteed to remain valid until a mutation is applied
/// to the [Graph].
abstract class GraphState {
  bool containsNodeId(NodeId id);
  bool containsEdgeId(EdgeId id);

  Iterable<NodeId> get nodeIds;
  Iterable<EdgeId> get edgeIds;

  /// Data generation methods for nodes.

  /// Returns an iterable of in/out-edge IDs for the given node [id].
  /// Implementations may return null or an empty list if [!containsNodeId(id)].
  Iterable<EdgeId> outEdgeIds(NodeId id);
  Iterable<EdgeId> inEdgeIds(NodeId id);

  Iterable<String> valueKeys(NodeId id);
  Uint8List getValue(NodeId id, String key);

  /// Data generation methods for edges.

  NodeId origin(EdgeId id);
  NodeId target(EdgeId id);
  Iterable<String> labels(EdgeId id);
}

class GraphMetadata {
  String debugName;
  String debugTraceId;

  @override
  String toString() => '$debugName (traceId:$debugTraceId)';
}

class GraphEvent {
  final Graph graph;
  final GraphMutationList mutations;
  final Uuid id;

  GraphEvent(this.graph, Iterable<GraphMutation> _mutations, {Uuid id})
      : mutations = new GraphMutationList.from(_mutations),
        id = id ?? new Uuid.random();

  factory GraphEvent.fromJson(final Graph graph, final List<dynamic> json) =>
      new GraphEvent(graph, json.map((m) => new GraphMutation.fromJson(m)));

  @override
  String toString() {
    return '$runtimeType\n' +
        mutations.map((GraphMutation m) => '  $m\n').join();
  }

  // TODO(ianloic): is there any reason to include the UUID in the JSON?
  dynamic toJson() => mutations.map((GraphMutation m) => m.toJson()).toList();
}
