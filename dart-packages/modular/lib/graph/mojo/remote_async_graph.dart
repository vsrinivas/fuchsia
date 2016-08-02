// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:async';

import 'package:modular_core/graph/async_graph.dart';
import 'package:modular_core/graph/buffering_mutator.dart';
import 'package:modular_core/graph/graph.dart';
import 'package:modular_core/graph/id.dart';
import 'package:modular_core/graph/mem_graph_state.dart';
import 'package:modular_core/graph/mixins/observation_mixin.dart';
import 'package:modular_core/graph/mixins/querying_mixin.dart';
import 'package:modular_core/graph/mutation.dart';

import '../../modular/graph.mojom.dart' as mojom;
import 'mutation_utils.dart';

/// A [RemoteAsyncGraph] mirrors a [Graph] that exists in an other Mojo
/// application. The mirror is kept in sync with the remote graph using a stream
/// of GraphMutation events.
///
/// A [RemoteAsyncGraph] can be used to mirror any [Graph] instance over mojo
/// that implements public/interfaces/graph.mojom. A common pattern is to
/// publish a graph using [GraphServer] (see graph_server.dart) and use a
/// [RemoteAsyncGraph] to mirror on the client-side.
class RemoteAsyncGraph extends AsyncGraphBase
    with GraphObservationMixin, GraphQueryingMixin
    implements mojom.GraphObserver {
  // The Mojo proxy to the remote graph that we're mirroring.
  final mojom.GraphProxy _proxy;

  // The Mojo stub that we pass to the remote application to subscribe to
  // notifications on [GraphMutation]s.
  final mojom.GraphObserverStub _observerStub =
      new mojom.GraphObserverStub.unbound();

  // Contains the current set of Nodes and Edges that are contained in this
  // graph.
  final MemGraphState _state = new MemGraphState();

  // Completer that runs when the graph has fully initialized.
  Completer _readyCompleter = new Completer();

  RemoteAsyncGraph(this._proxy)
      : nodeIdGenerator = new PrefixNodeIdGenerator(null),
        edgeIdGenerator = new PrefixEdgeIdGenerator(null) {
    assert(_proxy != null);
    _observerStub.impl = this;
    _proxy.addObserver(_observerStub);

    // TODO(armansito): Need some sort of error callback to notify users when
    // there is an error in the connection through this proxy, since that would
    // render this graph invalid. One possible requirement is that the caller
    // that creates this RemoteAsyncGraph watch the proxy directly for errors.
  }

  /// Closes the connection to the underlying endpoint. Call this to avoid
  /// leaking mojo handles.
  void close() {
    assert(_observerStub != null);
    _observerStub.close();
  }

  @override // mojom.GraphObserver
  void onChange(List<mojom.GraphMutation> mojomMutations, void callback()) {
    // Apply all the changes before notifying observers.
    final List<GraphMutation> mutations =
        mojomMutations.map(mojomMutationToDart).toList();
    mutations.forEach(_state.applyMutation);

    // If the graph was already initialized earlier, then send graph change
    // notifications to observers. Otherwise, we complete the ready future.
    if (isReady) {
      // TODO(armansito): Pass in a tag here? If we can know for a fact that a
      // notification has resulted because of a pending call to mutateAsync,
      // then we could reuse the tags that were passed to mutateAsync.
      notifyGraphChanged(mutations.toList());
    } else {
      _readyCompleter.complete();
    }

    callback();
  }

  /// Used for mixins since they cannot have a constructor and need a reference
  /// to the Graph they are mixed into.
  @override // GraphObservationMixin, GraphQueryingMixin
  Graph get mixinGraph => this;

  @override // Graph
  GraphState get state => _state;

  @override // Graph
  final NodeIdGenerator nodeIdGenerator;

  @override // Graph
  final EdgeIdGenerator edgeIdGenerator;

  @override // AsyncGraphBase
  bool get isReady => _readyCompleter.isCompleted;

  @override // AsyncGraphBase
  Future<Null> waitUntilReady() => _readyCompleter.future;

  @override // AsyncGraphBase
  Future<Null> mutateAsync(MutateGraphCallback fn, {dynamic tag}) {
    // Use a BufferingMutator to queue up and validate mutations based on the
    // current state of the graph, but do not actually apply them. We first send
    // the mojo message and update the graph data on our end when the remote
    // notifies us via GraphObserver.onChange.
    BufferingMutator mutator = new BufferingMutator(this);
    try {
      fn(mutator);
    } catch (e) {
      final completer = new Completer<Null>();
      completer.completeError(e);
      return completer.future;
    }
    return _applyBufferedMutations(mutator);
  }

  Future<Null> _applyBufferedMutations(BufferingMutator mutator) {
    // If there is nothing to apply, then return success;
    if (mutator.mutations.isEmpty) return new Future.value();

    Completer completer = new Completer();

    // Send the mutations over to the remote so they get handled remotely.
    _proxy.applyMutations(mutator.mutations.map(dartMutationToMojom).toList(),
        (mojom.GraphStatus status, String errorDescription) {
      if (status == mojom.GraphStatus.success) {
        completer.complete();
      } else {
        completer.completeError(new FailedGraphMutation(null,
            errorString: errorDescription,
            context: mutator.mutations.toList()));
      }
    });
    return completer.future;
  }
}
