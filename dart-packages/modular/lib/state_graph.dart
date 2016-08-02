// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/// Helper for manipulating the session graph abstracting away the Mojo API.

import 'dart:async';
import 'dart:typed_data';

import 'package:collection/collection.dart';
import 'package:modular_core/graph/async_graph.dart';
import 'package:modular_core/graph/buffering_mutator.dart';
import 'package:modular_core/graph/id.dart';
import 'package:modular_core/graph/lazy_cloned_graph.dart';
import 'package:modular_core/graph/mutation.dart';
import 'package:modular_core/graph/ref.dart';
import 'package:modular_core/log.dart';
import 'package:modular_core/util/timeline_helper.dart';

import 'representation_types.dart';

// TODO(armansito): Some of these exceptions are not thrown. Remove unused
// exceptions or figure out if they should be thrown and, if so, why they
// aren't.
class StateGraphException implements Exception {
  final String _moduleTag;

  const StateGraphException(this._moduleTag);

  @override
  String toString() => '$_moduleTag StateGraph exception. ';
}

class MultipleMatchingEdgesException extends StateGraphException {
  final List<String> _labels;
  final List<SemanticNode> _nodes;

  MultipleMatchingEdgesException(
      final String moduleTag, this._labels, this._nodes)
      : super(moduleTag);

  @override
  String toString() =>
      super.toString() +
      'Labels $_labels were matched by multiple edges pointing to the '
      'following semantic nodes: $_nodes.';
}

class MultipleRepresentationValuesException extends StateGraphException {
  final SemanticNode _node;

  MultipleRepresentationValuesException(final String moduleTag, this._node)
      : super(moduleTag);

  @override
  String toString() =>
      super.toString() +
      'Node $_node has multiple representation values so automatic getter '
      'cannot be used.';
}

class UndeclaredLabelException extends StateGraphException {
  final String _label;

  UndeclaredLabelException(final String moduleTag, this._label)
      : super(moduleTag);

  @override
  String toString() =>
      super.toString() +
      'Could not identify label \'$_label\'. Make sure it is declared in your '
      'module manifest.';
}

class UnauthorizedEdgeWriteException extends StateGraphException {
  final String _message;

  UnauthorizedEdgeWriteException(final String moduleTag, this._message)
      : super(moduleTag);

  @override
  String toString() =>
      super.toString() +
      'Module is not authorized to write the edge. Make sure the edge is '
      'declared in your module manifest. Error message from handler:'
      '\n\t$_message';
}

class IpcReturnedErrorException extends StateGraphException {
  final String _message;

  IpcReturnedErrorException(final String moduleTag, this._message)
      : super(moduleTag);

  @override
  String toString() =>
      super.toString() +
      'IPC call returned unexpected error. Please file a bug '
      'including the error message from handler: '
      '\n\t$_message';
}

/// Represents a single semantic node of the session graph.
class SemanticNode {
  final Node _node;
  final StateGraph _state;

  SemanticNode(this._state, this._node) {
    assert(_node != null);
    assert(_state != null);
  }

  NodeId get id => _node.id;
  bool get isDeleted => _node.isDeleted;

  // TODO(armansito): Need to think about obfuscating values that are marked as
  // "internal:" and also think about keeping the node ID opaque while logging.
  @override
  String toString() =>
      '<SemanticNode id=${_node.id} values=${_node.valueKeys}>';

  /// Creates a new edge from this node, with the provided [labels], to [node].
  /// If [node] is not provided or null, a new node will be created and
  /// returned.
  SemanticNode create(final List<String> labels, [SemanticNode node]) {
    if (node == null) {
      node = _state.addNode();
    }
    _state.addEdge(this, node, _toLabelUrls(labels));
    return node;
  }

  /// Deletes an edge with the provided [labels] between [this] node and the
  /// provided [node]. If [node] is not provided or null, all edges with
  /// [labels] from [this] are deleted.
  void delete(final List<String> labels, [final SemanticNode node]) {
    Iterable<Edge> edges =
        _node.outEdgesWithLabels(labels.map(_state.getLabelUrlAsString));
    if (node != null) {
      assert(node != this);
      assert(node._node != _node);
      edges = edges.where((final Edge e) => e.target == node._node);
    }
    _state.deleteEdges(edges);
  }

  /// Sets the unique edge from [this] node with the provided [labels] to the
  /// provided [node]. If there are already edges with the provided [labels],
  /// they will be deleted.
  SemanticNode set(final List<String> labels, [final SemanticNode node]) {
    delete(labels);
    return create(labels, node);
  }

  /// Gets the unique node with the provided [labels] originating from [this]
  /// node. An exception is thrown if multiple edges with [labels] are present.
  /// If no such node exists, then it creates one with the provided [labels].
  SemanticNode getOrDefault(final List<String> labels) {
    return get(labels) ?? set(labels);
  }

  /// Gets the unique node with the provided [labels] originating from [this]
  /// node. An exception is thrown if multiple edges with [labels] are present.
  SemanticNode get(final List<String> labels) {
    List<SemanticNode> nodes =
        getList(labels.map(_state.getLabelUrlAsString).toList());
    if (nodes.length == 0) {
      return null;
    } else if (nodes.length == 1) {
      return nodes[0];
    } else {
      throw new MultipleMatchingEdgesException(
          _state._moduleTag, labels, nodes);
    }
  }

  /// Gets a list of all nodes linked from [this] node through edges with the
  /// provided [labels].
  List<SemanticNode> getList(final List<String> labels) {
    if (isDeleted) return [];
    final neighbors = <SemanticNode>[];
    for (final Edge e
        in _node.outEdgesWithLabels(labels.map(_state.getLabelUrlAsString))) {
      neighbors.add(new SemanticNode(_state, e.target));
    }
    return neighbors;
  }

  dynamic get value {
    if (_node.valueKeys.isEmpty) return null;
    if (_node.valueKeys.length > 1) {
      // TODO(armansito): Should we just pick one and return that? Weird to have
      // this AND read() below. Which one should a developer use? Do we expect a
      // developer to choose between this and read() by conditionally checking
      // how many representation values there are? Sounds awkward.
      throw new MultipleRepresentationValuesException(_state._moduleTag, this);
    }

    final String representationLabel = _node.valueKeys.single;
    final Uint8List representationValue = _node.getValue(representationLabel);

    return _state._bindingsRegistry.read(new RepresentationValue(
        Uri.parse(representationLabel), representationValue));
  }

  set value(dynamic valueToSet) {
    RepresentationValue representationValue =
        _state._bindingsRegistry.write(valueToSet);
    write(representationValue.label.toString(), representationValue.data);
  }

  /// Reads the representation data corresponding to the given representation
  /// label.
  Uint8List read(final String representationLabel) {
    return _node.getValue(_state.getLabelUrlAsString(representationLabel));
  }

  /// Writes the given representation data under the given representation label.
  void write(final String representationLabel, final Uint8List data) {
    _state.setRepresentationValue(
        this, data, _state.getLabelUrl(representationLabel));
  }

  // Returns the id of the node as stored in the session graph.
  String getNodeId() => '$id';

  List<Uri> _toLabelUrls(final List<String> labels) {
    return labels
        .map((final String label) => _state.getLabelUrl(label))
        .toList();
  }
}

/// Communicates with the session service and manipulates the session graph.
class StateGraph {
  static final Logger _log = log('modular.StateGraph');

  // The graph that contains the footprint of the module this [StateGraph] is
  // associated with.
  final AsyncGraph _remoteGraph;

  // HACK(armansito): Modules currently rely on graph modifications being
  // immediately reflected locally before calling push (e.g. by expecting to
  // access the same SemanticNode via getOrDefault). This is a hack to keep a
  // local copy of the remote graph where we can dump changes before sending
  // them to the remote.
  // TODO(armansito): Remove the local graph and rethink how modules should
  // access semantic nodes, e.g. should a node be persisted locally if it hasn't
  // been pushed to the Handler yet?
  LazyClonedGraph _localGraph;

  // The current mutator that is buffering a list of changes that will be sent
  // to the remote graph on the next call to [StateGraph.push()]. If [_mutator]
  // is null, then there are no buffered mutations. Calling "push()" below sets
  // this to null to mark the pending buffered mutations as "flushed".
  BufferingMutator _mutator;
  BufferingMutator _getOrCreateMutator() {
    if (_mutator == null) {
      // We use a non-validating mutator here, since the actual validation is
      // performed in the Handler. We are using a BufferingMutator as a
      // convenient way to create and store pending mutations.
      _mutator = new BufferingMutator(_remoteGraph, validating: false,
          onEachMutation: (final GraphMutation mutation) {
        assert(_localGraph != null);
        _localGraph.mutate((final GraphMutator m) => m.apply(mutation));
      });
    }
    return _mutator;
  }

  // Label maps.
  final Map<String, Uri> _labelShorthandToUrl = <String, Uri>{};

  final RepresentationBindingsRegistry _bindingsRegistry;

  /// Tag to be included in error messages and traces. This can be used to
  /// identify the module that owns this state graph.
  final String _moduleTag;

  StateGraph(this._remoteGraph, final Map<String, String> labelShorthandToUrl,
      {RepresentationBindingsRegistry customBindingsRegistry, String moduleTag})
      : _bindingsRegistry = customBindingsRegistry ?? bindingsRegistry,
        _moduleTag = moduleTag ?? "unknown module" {
    assert(_remoteGraph != null);
    assert(labelShorthandToUrl != null);
    labelShorthandToUrl.forEach((final String shorthand, final String url) {
      _labelShorthandToUrl[shorthand] = Uri.parse(url);
    });
    _localGraph = new LazyClonedGraph(_remoteGraph);
  }

  Uri getLabelUrl(String shorthandOrUrl) {
    if (_labelShorthandToUrl.containsKey(shorthandOrUrl)) {
      // It's a shorthand.
      return _labelShorthandToUrl[shorthandOrUrl];
    }

    // If it's not a declared shorthand, it has to be a real url.
    Uri uri = Uri.parse(shorthandOrUrl);
    if (uri.scheme == null || uri.scheme.isEmpty) {
      throw new UndeclaredLabelException(_moduleTag, shorthandOrUrl);
    }
    return uri;
  }

  String getLabelUrlAsString(String shorthandOrUrl) {
    return getLabelUrl(shorthandOrUrl)?.toString();
  }

  /// Adds a new node to the graph. The returned object can be used to identify
  /// the node in subsequent calls to addEdge().
  SemanticNode addNode() {
    Node node = _getOrCreateMutator().addNode();
    return new SemanticNode(this, _localGraph.node(node.id));
  }

  /// Adds an edge labeled with the indicated semantic label to the session
  /// graph.
  void addEdge(
      final SemanticNode from, final SemanticNode to, final List<Uri> labels) {
    final EdgeId edgeId = _remoteGraph.edgeIdGenerator();
    final GraphMutation mutation = new GraphMutation.addEdge(edgeId, from.id,
        to.id, labels.map((final Uri uri) => uri.toString()).toList());
    _getOrCreateMutator().apply(mutation);
  }

  void deleteEdges(final Iterable<Edge> edges) {
    for (final Edge e in edges) {
      _getOrCreateMutator().removeEdge(e.id);
    }
  }

  /// Returns semantic nodes reachable from following edges containing exactly
  /// the required labels
  Iterable<SemanticNode> getNeighbors(final List<Uri> requiredLabels) {
    final Set<String> labels =
        requiredLabels.map((final Uri uri) => uri.toString()).toSet();
    return _localGraph.edges
        .where((final Edge e) => const SetEquality<String>()
            .equals(new Set<String>.from(e.labels), labels))
        .map((final Edge e) => new SemanticNode(this, e.target));
  }

  /// Adds to the given semantic node the given representation value.
  void setRepresentationValue(final SemanticNode node, final Uint8List value,
      final Uri representationLabel) {
    _getOrCreateMutator()
        .setValue(node.id, representationLabel.toString(), value);
  }

  /// Return all nodes without ancestors
  /// TODO(etiennej): These are not real anchors in the Modular Simplified sense
  /// and should be updated when addressing
  /// https://github.com/domokit/modular/issues/593
  Iterable<SemanticNode> get anchors {
    // Exhaust the iterable right away, in case the graph gets modified before
    // it gets used (via toList()).
    return _localGraph.nodes
        .where((final Node n) => n.inEdges.isEmpty)
        .map((final Node n) => new SemanticNode(this, n))
        .toList();
  }

  SemanticNode get root => anchors.first;

  /// Writes the graph to the session service. The calls are put in a queue and
  /// handled asynchronously, but each call will push exactly the state of the
  /// graph at the moment of calling .push().
  Future<Null> push() {
    // The snippet below executes asynchronously, so we set |_mutator| to null
    // here to avoid sending the same mutations more than once.
    final List<GraphMutation> mutations = _mutator?.mutations;
    _mutator = null;

    return traceAsync('$runtimeType $_moduleTag push()', () async {
      if (mutations == null || mutations.isEmpty) {
        _log.info('No changes to push. Nothing to do');
        return;
      }

      try {
        await _remoteGraph.mutateAsync((final GraphMutator mutator) {
          mutations.forEach(mutator.apply);
        });

        _localGraph = new LazyClonedGraph(_remoteGraph);
      } catch (e, stackTrace) {
        throw new IpcReturnedErrorException(_moduleTag, '$e\n$stackTrace');
      }
    });
  }
}
