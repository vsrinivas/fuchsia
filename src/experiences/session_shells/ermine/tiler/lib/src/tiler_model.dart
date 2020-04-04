// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:flutter/material.dart';
import 'package:flutter/scheduler.dart';

import 'tile_model.dart';
import 'utils.dart';

/// Defines a model to hold a tree of [TileModel]s. The leaf nodes are tiles
/// of type [TileType.content], branches can be [TileType.column] or
/// [TileType.row]. The member [root] holds the reference to the root tile.
class TilerModel<T> extends ChangeNotifier {
  final TileModel<T> _root;
  final List<TileModel<T>> _floats;

  TilerModel({TileModel<T> root, List<TileModel<T>> floats})
      : _root = root ?? TileModel<T>(type: TileType.column),
        _floats = floats ?? <TileModel<T>>[] {
    if (!_root.isCollection) {
      throw ArgumentError('root of tiles should be a collection type');
    }
    traverse(tile: _root, callback: (tile, parent) => tile.parent = parent);
  }

  TileModel<T> get root => _root;

  List<TileModel<T>> get floats => _floats;

  /// Returns the tile next to [tile] in the given [direction].
  TileModel<T> navigate(AxisDirection direction, [TileModel<T> tile]) {
    if (tile == null) {
      return _first(root);
    } else if (tile.isFloating && floats.contains(tile)) {
      if (axisDirectionIsReversed(direction)) {
        return floats.first != tile ? floats[floats.indexOf(tile) - 1] : null;
      } else {
        return floats.last != tile ? floats[floats.indexOf(tile) + 1] : null;
      }
    }

    switch (direction) {
      case AxisDirection.left:
        return _last(_previous(tile, TileType.column));
      case AxisDirection.up:
        return _last(_previous(tile, TileType.row));
      case AxisDirection.right:
        return _first(_next(tile, TileType.column));
      case AxisDirection.down:
        return _first(_next(tile, TileType.row));
      default:
        return null;
    }
  }

  /// Adds a new tile with [content] next to currently focused tile in the
  /// [direction] specified.
  TileModel<T> add({
    TileModel<T> nearTile,
    T content,
    AxisDirection direction = AxisDirection.right,
    Size minSize = Size.zero,
  }) {
    nearTile ??= root;

    // Create a TileModel for [content].
    final tile = TileModel<T>(
      type: TileType.content,
      content: content,
      minSize: minSize,
    );

    final type = _isHorizontal(direction) ? TileType.column : TileType.row;

    void _insert(TileModel<T> nearTile, TileModel<T> tile) {
      final parent = nearTile.parent;
      if (parent == null) {
        // [nearTile] is root and needs to be kept as such. Create a copy of it
        // and move it one level down along with [tile].
        assert(nearTile.isCollection);
        if (nearTile.type == type) {
          _setFlex(nearTile, tile);

          nearTile.insert(
              axisDirectionIsReversed(direction) ? 0 : nearTile.tiles.length,
              tile);
        } else if (nearTile.tiles.isEmpty) {
          nearTile
            ..insert(0, tile)
            ..type = type;
        } else {
          final copy = TileModel<T>(
            type: nearTile.type,
          );
          // Move child tiles from [nearTile] to [copy].
          for (final tile in nearTile.tiles.toList()) {
            nearTile.remove(tile);
            copy.add(tile);
          }
          nearTile
            ..add(axisDirectionIsReversed(direction) ? tile : copy)
            ..add(axisDirectionIsReversed(direction) ? copy : tile)
            ..type = type;
        }
      } else {
        if (parent.type == type || parent.tiles.length == 1) {
          _setFlex(parent, tile);
          int index = parent.tiles.indexOf(nearTile);
          parent
            ..insert(
                axisDirectionIsReversed(direction) ? index : index + 1, tile)
            ..type = type;
        } else {
          // The parent's type is not aligned with direction. Walk up the tree
          // to find an ancestor with matching type (or creating one, if none is
          // found at the root).
          _insert(parent, tile);
        }
      }
    }

    _insert(nearTile, tile);
    return tile;
  }

  /// Returns a new tile after splitting the supplied [tile] in the [direction].
  /// The [tile] is split such that new tile has supplied [flex].
  TileModel<T> split({
    @required TileModel<T> tile,
    T content,
    AxisDirection direction = AxisDirection.right,
    double flex = 0.5,
    Size minSize = Size.zero,
  }) {
    assert(tile != null && tile.isContent);
    assert(flex > 0 && flex < 1);

    final newTile = TileModel<T>(
      parent: tile,
      type: TileType.content,
      content: content,
      flex: flex,
      minSize: minSize,
    );

    final parent = tile.parent;
    int index = parent?.tiles?.indexOf(tile) ?? 0;
    parent?.remove(tile);

    final newParent = TileModel<T>(
      type: _isHorizontal(direction) ? TileType.column : TileType.row,
      flex: tile.flex,
      tiles: axisDirectionIsReversed(direction)
          ? [newTile, tile]
          : [tile, newTile],
    );

    parent?.insert(index, newParent);
    tile.flex = 1 - flex;

    return newTile;
  }

  /// Removes the [tile] and it's parent if it is empty, recursively. Does not
  /// remove the root. If root type was [TileType.content], converts it to
  /// a collection type [TileType.column].
  void remove(TileModel<T> tile) {
    assert(tile != null);

    void _remove(TileModel<T> tile) {
      final parent = tile.parent;
      // Don't remove root tile.
      if (parent != null) {
        parent.remove(tile);
        // Remove empty parent.
        if (parent.tiles.isEmpty) {
          _remove(parent);
        } else if (parent.tiles.length == 1 && parent.parent != null) {
          // Move the only child to it's grandparent.
          final child = parent.tiles.first;
          final grandparent = parent.parent;
          final index = grandparent.tiles.indexOf(parent);
          parent.remove(child);
          grandparent
            ..remove(parent)
            ..insert(index, child);
        }
      }
    }

    _remove(tile);
  }

  void floatTile(TileModel<T> tile) {
    tile.parent.tiles.remove(tile);
    tile.parent = null;
    floats.add(tile);

    SchedulerBinding.instance.scheduleTask(notifyListeners, Priority.idle);
  }

  /// Returns the tile next to [tile] in a column or row. If [tile] is the last
  /// tile in the parent, it returns the next ancestor column or row. This is
  /// usefule for finding the tile to the right or below the given [tile].
  TileModel _next(TileModel tile, TileType type) {
    if (tile == null || tile.parent == null) {
      return null;
    }
    assert(tile.parent.tiles.contains(tile));

    final tiles = tile.parent.tiles;
    if (tile.parent.type == type && tile != tiles.last) {
      int index = tiles.indexOf(tile);
      return tiles[index + 1];
    }
    return _next(tile.parent, type);
  }

  /// Returns the tile previous to [tile] in a column or row. If [tile] is the
  /// last tile in the parent, it returns the previous ancestor column or row.
  /// This is useful for finding the tile to the left or above the given [tile].
  TileModel _previous(TileModel tile, TileType type) {
    if (tile == null || tile.parent == null) {
      return null;
    }
    assert(tile.parent.tiles.contains(tile));

    final tiles = tile.parent.tiles;
    if (tile.parent.type == type && tile != tiles.first) {
      int index = tiles.indexOf(tile);
      return tiles[index - 1];
    }
    return _previous(tile.parent, type);
  }

  /// Returns the leaf tile node given a [tile] using depth first search.
  TileModel _first(TileModel tile) {
    if (tile == null || tile.isContent) {
      return tile;
    }
    return _first(tile.tiles.first);
  }

  /// Returns the leaf tile node given a [tile] using depth last search.
  TileModel _last(TileModel tile) {
    if (tile == null || tile.isContent) {
      return tile;
    }
    return _last(tile.tiles.last);
  }

  bool _isHorizontal(AxisDirection direction) =>
      direction == AxisDirection.left || direction == AxisDirection.right;

  // ignore:unused_element
  bool _isVertical(AxisDirection direction) =>
      direction == AxisDirection.up || direction == AxisDirection.down;

  void _setFlex(TileModel<T> parent, TileModel<T> tile) {
    // If all existing children have same flex, then set the [tile]'s
    // flex to be the same, thus equally sub-dividing the parent.
    if (!parent.isEmpty &&
        parent.tiles.every((tile) => parent.tiles.first.flex == tile.flex)) {
      tile.flex = parent.tiles.first.flex;
    }
  }
}
