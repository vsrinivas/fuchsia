// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:collection';

import 'tile_model.dart';
import 'tiler_model.dart';

Iterable<TileModel<T>> flatten<T>(Iterable<TileModel<T>> tiles) {
  return tiles
      .map((tile) => tile.isContent ? [tile] : flatten(tile.tiles))
      .expand((t) => t);
}

/// A co-routine to iterate over the TilerModel tree.
Iterable<TileModel> tilerWalker(TilerModel a) sync* {
  final nodes = ListQueue<TileModel>()..add(a.root);
  while (nodes.isNotEmpty) {
    nodes.addAll(nodes.first.tiles);
    yield nodes.removeFirst();
  }
}

/// Get the content nodes of a layout tree.
List<TileModel> getTileContent(TilerModel a) => tilerWalker(a)
    .where((t) => t.type == TileType.content)
    .fold(<TileModel>[], (l, t) => l..add(t));

enum Traversal { depthFirst, depthLast }

/// Traverse the tree rooted at [tile] in [order] and call [callback] at each
/// node, unless [contentOnly] was set to [true].
void traverse<T>({
  TileModel<T> tile,
  Traversal order = Traversal.depthFirst,
  bool contentOnly = false,
  void callback(TileModel<T> tile, TileModel<T> parent),
}) {
  void _traverse(TileModel<T> t, TileModel<T> p) {
    if (t.isContent || !contentOnly) {
      callback(t, p);
    }
    final it = order == Traversal.depthFirst ? t.tiles : t.tiles.reversed;
    for (final c in it) {
      _traverse(c, t);
    }
  }

  _traverse(tile, tile.parent);
}
