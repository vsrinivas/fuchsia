// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:modular_core/log.dart';
import 'package:modular_core/graph/graph.dart' as graph;
import 'package:modular_core/graph/mutation.dart' as graph;
import 'package:modular_core/graph/ref.dart' as graph;
import 'package:mojo/core.dart';

import '../../modular/graph.mojom.dart' as mojom;
import 'mutation_utils.dart';

/// A [GraphServer] publishes a single [Graph] instance over mojo,
/// making graph operations over local [Graph] objects available to
/// remote clients.
///
/// [GraphServer] implements the mojo interface defined in
/// public/interfaces/graph.mojom.
class GraphServer implements mojom.Graph {
  final Logger _log = log('graph.GraphServer');
  final graph.Graph hostedGraph;
  mojom.GraphStub _stub;

  final List<mojom.GraphObserverProxy> _remoteObservers =
      <mojom.GraphObserverProxy>[];

  GraphServer(this.hostedGraph);

  GraphServer.fromEndpoint(
      final MojoMessagePipeEndpoint endpoint, this.hostedGraph) {
    assert(hostedGraph != null);
    _stub = new mojom.GraphStub.fromEndpoint(endpoint, this);
  }

  /// Closes the connection to the underlying endpoint. Call this to avoid
  /// leaking mojo handles if this instance was constructed using the
  /// [GraphServer.fromEndpoint] constructor.
  void close() {
    if (_stub != null) _stub.close();
  }

  @override // mojom.Graph
  void addObserver(Object observerProxyObject) {
    final mojom.GraphObserverProxy observerProxy =
        observerProxyObject as mojom.GraphObserverProxy;

    // Add the remote proxy to our list and also set it up so that it gets
    // removed if the assocated pipe gets disconnected.
    _addObserver(observerProxy);
    observerProxy.ctrl.errorFuture.then((dynamic error) {
      _log.warning('Proxy error: $error');
      _removeObserver(observerProxy);
    });

    // Send an initial notification to let the client sync the whole graph.
    // TODO(armansito): We should add a filter parameter to addObserver and
    // construct that notification based on that filter, since not every client
    // will be interested in syncing up a whole graph.
    // TODO(armansito): Add a method to Graph that returns its contents in the
    // form of a sequence of GraphMutations.
    final mutations = <graph.GraphMutation>[];
    for (final graph.Node node in hostedGraph.nodes) {
      mutations.add(new graph.GraphMutation.addNode(node.id));
      for (final String valueKey in node.valueKeys) {
        mutations.add(new graph.GraphMutation.setValue(
            node.id, valueKey, node.getValue(valueKey)));
      }
    }

    for (final graph.Edge edge in hostedGraph.edges) {
      mutations.add(new graph.GraphMutation.addEdge(
          edge.id, edge.origin.id, edge.target.id, edge.labels));
    }

    observerProxy.onChange(mutations.map(dartMutationToMojom).toList(), () {});
  }

  @override // mojom.Graph
  void applyMutations(final List<mojom.GraphMutation> mojomMutations,
      void callback(mojom.GraphStatus status, String errorDescription)) {
    try {
      hostedGraph.mutate((graph.GraphMutator mutator) {
        for (final mojom.GraphMutation m in mojomMutations) {
          mutator.apply(mojomMutationToDart(m));
        }
      });
      callback(mojom.GraphStatus.success, null);
    } catch (exception) {
      callback(mojom.GraphStatus.failure, '$exception');
    }
  }

  void _addObserver(final mojom.GraphObserverProxy observerProxy) {
    _remoteObservers.add(observerProxy);
    if (_remoteObservers.length == 1) hostedGraph.addObserver(_onGraphChanged);
  }

  void _removeObserver(final mojom.GraphObserverProxy observerProxy) {
    _remoteObservers.remove(observerProxy);
    if (_remoteObservers.isEmpty) hostedGraph.removeObserver(_onGraphChanged);
  }

  void _onGraphChanged(graph.GraphEvent event) {
    final List<mojom.GraphMutation> mojomMutations =
        event.mutations.map(dartMutationToMojom).toList();
    _remoteObservers.forEach((proxy) => proxy.onChange(mojomMutations, () {}));
  }
}
