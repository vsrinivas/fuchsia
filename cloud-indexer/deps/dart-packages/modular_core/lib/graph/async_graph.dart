// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:async';
import 'dart:core';

import 'graph.dart';
import 'graph_base.dart';

/// An [AsyncGraph] is a variant of [Graph] that is initialized and performs
/// mutation functions asynchronously.
abstract class AsyncGraph extends Graph {
  /// Returns whether or not this graph has been initialized and its contents
  /// are ready to access.
  bool get isReady;

  /// Returns a Future that completes when the graph has been initialized and
  /// its contents are ready for access.
  Future<Null> waitUntilReady();

  /// Performs the same operation as [Graph.mutate], however it reports the
  /// result of the operation asynchronously when the returned Future completes.
  /// Callers can store the node and edge refs that are returned by the
  /// [GraphMutator] that gets passed to [fn], if any, however these are not
  /// considered to be valid until the returned [Future] completes successfully.
  /// I.e. the refs are consistent within the scope of the [GraphMutator] that
  /// gets passed to [fn], so they can be reused during a call to [mutateAsync].
  /// The same refs should not be used to refer to nodes/edges in the Graph
  /// after the [mutateAsync] returns but before the returned [Future]
  /// completes.
  ///
  /// Unlike [Graph.mutate], [mutateAsync] does NOT guarantee that [fn] will
  /// execute synchronously. It may be called asynchronously at a later
  /// message-loop time slice. The only guarantee is that [fn] will be called
  /// before the returned [Future] completes.
  Future<Null> mutateAsync(MutateGraphCallback fn, {dynamic tag});
}

abstract class AsyncGraphBase extends GraphBase implements AsyncGraph {
  @override // Graph
  void mutate(MutateGraphCallback fn, {dynamic tag}) =>
      throw new UnsupportedError('Synchronous mutations not supported');
}
