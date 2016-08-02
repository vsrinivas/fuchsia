// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:async';
import 'dart:collection';

import 'package:modular_services/ledger2/ledger2.mojom.dart';
import 'package:modular/modular/graph.mojom.dart' as mojom;
import 'package:modular_core/uuid.dart' show Uuid;

import 'package:modular_core/graph/async_graph.dart';
import 'package:modular_core/graph/buffering_mutator.dart';
import 'package:modular_core/graph/graph.dart';
import 'package:modular_core/graph/id.dart';
import 'package:modular_core/graph/mem_graph_state.dart';
import 'package:modular/graph/mojo/mutation_utils.dart';
import 'package:modular_core/graph/mixins/observation_mixin.dart';
import 'package:modular_core/graph/mixins/querying_mixin.dart';
import 'package:modular_core/graph/mutable_graph_state.dart';
import 'package:modular_core/graph/validating_lazy_cloned_graph_state.dart';
import 'package:modular_core/log.dart';

class _Transaction {
  GraphEvent event;
  dynamic tag;
  Completer<Null> completer;

  _Transaction(this.event, this.tag, this.completer);
}

/// A [LedgerGraph] combines a [LedgerMutator] and [LedgerObserver] to mirror
/// and interact with the graph for a particular session.  The [LedgerObserver]
/// receives notifications of both changes made by others, and the success or
/// failure of changes made by the [LedgerMutator].  This allows all mutations
/// to occur in the same order on each replica of the graph.
class LedgerGraph extends AsyncGraphBase
    with GraphObservationMixin, GraphQueryingMixin
    implements LedgerObserver {
  final Logger _log = log('handler.LedgerGraph');

  final LedgerMutatorProxy _mutatorProxy;
  final LedgerObserverStub _observerStub;

  /// [_state] is the current, observable state of this graph.  Fundamentally,
  /// this is a hack based on SessionGraphImpl's requirement for its writes to
  /// the ledger to be immediately reflected in the LedgerGraph.  Ideally,
  /// changes would only manifest when notifications from the ledger are
  /// received.  An example of the type of difficulty that might arise is when
  /// the local copy becomes incompatible with a mutation received from the
  /// ledger (such as if a Node is deleted in the local copy, and we are then
  /// notified of the creation of an Edge to or from that Node).
  final MemGraphState _state = new MemGraphState();

  /// Holds all transactions that have been sent to the ledger, for which the
  /// result is not yet known.
  final Queue<_Transaction> _transactions = new Queue<_Transaction>();

  LedgerGraph(this._mutatorProxy, this._observerStub)
      : nodeIdGenerator = new PrefixNodeIdGenerator(null),
        edgeIdGenerator = new PrefixEdgeIdGenerator(null) {
    assert(_mutatorProxy != null);
    assert(_observerStub != null);
    _observerStub.impl = this;

    // TODO(jjosh): Need some sort of error callback to notify users when
    // there is an error in the connection through the proxy/stub, since that
    // would render this graph invalid.
  }

  @override // AsyncGraphBase
  Future<Null> mutateAsync(MutateGraphCallback fn, {dynamic tag}) {
    // Use a BufferingMutator to queue up and validate mutations based on the
    // current state of the graph, but do not actually apply them. We first send
    // the mojo message and update the graph data on our end when the remote
    // notifies us via LedgerObserver.onChange.
    final mutator = new BufferingMutator(this, allowRedundantMutations: true);
    try {
      fn(mutator);
      if (mutator.mutations.isEmpty) return new Future.value();
    } catch (e) {
      final completer = new Completer<Null>();
      completer.completeError(e);
      return completer.future;
    }

    // TODO(jjosh): ideally we wouldn't apply these immediately, but
    // SessionGraphImpl expects us to.
    mutator.mutations.forEach(_state.applyMutation);
    notifyGraphChanged(mutator.mutations.toList());

    // TODO(jjosh): should we notify the ledger with the original or filtered
    // list of mutations?  There are pros and cons.  Probably better to not
    // worry about it, and aim to not keep a local copy of state here.
    final _Transaction transaction = new _Transaction(
        new GraphEvent(this, mutator.mutations, id: new Uuid.random()),
        tag,
        new Completer<Null>());

    _dispatchTransaction(transaction);
    return transaction.completer.future;
  }

  // TODO(jjosh): hack
  @override // AsyncGraphBase
  void mutate(MutateGraphCallback fn, {dynamic tag}) {
    mutateAsync(fn, tag: tag);
  }

  void _dispatchTransaction(_Transaction transaction) {
    _transactions.add(transaction);
    _mutatorProxy.applyEvent(dartEventToMojom(transaction.event));
  }

  @override // mojom.LedgerObserver
  void onSuccess(mojom.GraphEvent mojoEvent) {
    Completer<Null> completer;
    try {
      final GraphEvent event = mojomEventToDart(mojoEvent);
      _checkTransactionId(event);
      final _Transaction transaction = _transactions.removeFirst();
      completer = transaction.completer;

      // Notify listeners even though we already did in mutateAsync().  This is
      // another vaguely dodgy consequence of keeping a local copy.
      final mutations = _validateAndApply(event.mutations, _state);
      notifyGraphChanged(mutations, tag: transaction.tag);

      completer.complete();
    } catch (error) {
      completer?.completeError(error);
      close();
      throw error;
    }
  }

  @override // mojom.LedgerObserver
  void onFailure(mojom.GraphEvent mojoEvent, mojom.GraphMutation failed) {
    Completer<Null> completer;
    try {
      final GraphEvent event = mojomEventToDart(mojoEvent);
      final GraphMutation failedMutation = mojomMutationToDart(failed);
      _checkTransactionId(event);
      final _Transaction transaction = _transactions.removeFirst();
      // TODO(jjosh): figure out why this happened and how to recover from it.
      final errorMessage = 'failed to commit to ledger event: $event '
          'failedMutation: $failedMutation';
      _log.warning(errorMessage);
      transaction.completer.completeError(errorMessage);
    } catch (error) {
      completer?.completeError(error);
      close();
      throw error;
    }
  }

  @override // mojom.LedgerObserver
  void onChange(mojom.GraphEvent mojoEvent) {
    final GraphEvent event = mojomEventToDart(mojoEvent);
    final mutations = _validateAndApply(event.mutations, _state);
    notifyGraphChanged(mutations);
  }

  void _checkTransactionId(GraphEvent event) {
    if (_transactions.isEmpty)
      throw 'received event, but no transaction was pending (id: ${event.id})';
    if (_transactions.first.event.id != event.id)
      throw 'received event with unexpected id: '
          '${event.id} instead of ${_transactions.first.event.id}';
  }

  List<GraphMutation> _validateAndApply(
      GraphMutationList mutations, MutableGraphState state) {
    final validator = new ValidatingLazyClonedGraphState(_state,
        allowRedundantMutations: true);
    final validated = mutations.where(validator.applyMutation).toList();
    validated.forEach(_state.applyMutation);
    return validated;
  }

  // TODO(jjosh): implement.
  @override // AsyncGraph
  Future<Null> waitUntilReady() => new Future.value();

  // TODO(jjosh): implement.
  @override // AsyncGraph
  bool get isReady => true;

  /// Closes the connection to the underlying endpoint. Call this to avoid
  /// leaking mojo handles.
  void close() {
    _mutatorProxy.close();
    _observerStub.close();
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
}
