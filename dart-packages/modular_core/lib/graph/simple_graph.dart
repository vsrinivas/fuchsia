// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'graph.dart';
import 'graph_base.dart';
import 'mutable_graph_state.dart';
import 'mutation.dart';
import 'id.dart';
import 'mixins/buffered_mutate_mixin.dart';
import 'mixins/observation_mixin.dart';
import 'mixins/querying_mixin.dart';

/// A simple concrete implementation of [Graph].  If modified via mutate(), then
/// [BufferedMutateMixin] guarantees that the state isn't harmed when an invalid
/// mutation is attempted.  However, when applyMutations() is called directly,
/// an exception may be thrown with only some of the mutations applied.  In such
/// cases, it is the caller's responsibility to ensure in advance that the
/// mutations are valid, or to manually roll back to a sane state, or similar.
class SimpleGraph extends GraphBase
    with GraphObservationMixin, GraphQueryingMixin, BufferedMutateMixin {
  final MutableGraphState _state;

  SimpleGraph(this._state, this.nodeIdGenerator, this.edgeIdGenerator);

  /// Used for mixins since they cannot have a constructor and need a reference
  /// to the Graph they are mixed into.
  @override // GraphObservationMixin, GraphQueryingMixin, BufferedMutateMixin
  Graph get mixinGraph => this;

  @override
  GraphState get state => _state;

  @override // Graph
  final NodeIdGenerator nodeIdGenerator;

  @override // Graph
  final EdgeIdGenerator edgeIdGenerator;

  @override // BufferedMutateMixin
  void applyMutations(List<GraphMutation> mutations, {dynamic tag}) {
    mutations.forEach(_state.applyMutation);
    notifyGraphChanged(mutations, tag: tag);
  }
}
