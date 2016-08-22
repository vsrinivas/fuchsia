// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import '../graph.dart';
import '../mutation.dart';

abstract class GraphObservationMixin {
  Graph get mixinGraph;

  // Set of observers, and the current mutations to send. The mutations are
  // kept per observer. When an observer mutates the graph in the callback,
  // the new mutations are added to every element, and all the observers are
  // called in order. This ensures the following property:
  // - All mutations are always notified to observer in the registration order.
  Map<GraphChangeCallback, List<GraphMutation>> _observers =
      new Map<GraphChangeCallback, List<GraphMutation>>();

  int get observerCount => _observers.length;

  // TODO(thatguy): add coalesceMutations flag.
  void addObserver(GraphChangeCallback callback) {
    _observers[callback] = new List<GraphMutation>();
  }

  void removeObserver(GraphChangeCallback callback) {
    _observers.remove(callback);
  }

  /// Sets [event.graph] to [this] and calls each observer with [event].
  void notifyGraphChanged(final List<GraphMutation> mutations, {dynamic tag}) {
    for (GraphMutation mutation in mutations) {
      final GraphMutation taggedMutation =
          new GraphMutation.fromMutationWithOverrides(mutation, tag: tag);
      for (final List<GraphMutation> mutations in _observers.values) {
        mutations.add(taggedMutation);
      }
    }

    _notifyInternal();
  }

  void _notifyInternal() {
    while (_notifyOnce()) {}
  }

  bool _notifyOnce() {
    for (GraphChangeCallback observer in _observers.keys) {
      List<GraphMutation> mutations = _observers[observer];
      if (mutations.isNotEmpty) {
        List<GraphMutation> toSend = new List<GraphMutation>.from(mutations);
        mutations.clear();
        observer(new GraphEvent(mixinGraph, toSend));
        return true;
      }
    }
    return false;
  }
}
