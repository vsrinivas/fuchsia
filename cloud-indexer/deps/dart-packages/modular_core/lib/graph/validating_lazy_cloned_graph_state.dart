// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'graph.dart';
import 'lazy_cloned_graph.dart';
import 'validating_mutable_graph_state.dart';

/// Wrap a [MutableGraphState] with a [LazyClonedGraphState], and wrap that
/// with a [ValidatingGraphState].  Convenient for validating a list of
/// mutations before applying them to the base graph.  Inherits the caveats of
/// [LazyClonedGraphState].
class ValidatingLazyClonedGraphState extends ValidatingMutableGraphState {
  ValidatingLazyClonedGraphState._(
      LazyClonedGraphState state, bool allowRedundantMutations)
      : super(state, allowRedundantMutations: allowRedundantMutations);

  /// Wrap the provided state.
  factory ValidatingLazyClonedGraphState(
      GraphState state, {bool allowRedundantMutations: false}) =>
    new ValidatingLazyClonedGraphState._(
        new LazyClonedGraphState(state),
        allowRedundantMutations);

  /// Wrap the state obtained from the provided graph.
  factory ValidatingLazyClonedGraphState.forGraph(
      Graph graph, {bool allowRedundantMutations: false}) =>
    new ValidatingLazyClonedGraphState._(
        new LazyClonedGraphState.forGraph(graph),
        allowRedundantMutations);
}
