// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:math';
import 'package:flutter/material.dart';

import 'utils.dart';

enum TileType { content, row, column, custom }

/// Defines a model for a tile. It is a tree data structure where each tile
/// model holds a reference to its parent and a list of children. If the type
/// of tile, [TileType] is [TileType.content], it is a leaf node tile.
///
/// The [content] of tile holds a reference to an arbitrary object, that is
/// passed to the caller during the construction of the tile's chrome widget.
class TileModel<T> extends ChangeNotifier {
  TileModel<T> parent;
  TileType _type;
  T content;
  double flex;
  Offset position;
  final Size _minSize;
  List<TileModel<T>> tiles;

  TileModel({
    @required TileType type,
    this.parent,
    this.content,
    this.tiles,
    this.flex = 1,
    this.position = Offset.zero,
    Size minSize = Size.zero,
  })  : _minSize = minSize,
        _type = type {
    tiles ??= <TileModel<T>>[];
    traverse(tile: this, callback: (tile, parent) => tile.parent = parent);
  }

  TileType get type => _type;
  set type(TileType value) {
    _type = value;
    notifyListeners();
  }

  bool get isContent => type == TileType.content;
  bool get isFloating => parent == null;
  bool get isRow => type == TileType.row;
  bool get isColumn => type == TileType.column;
  bool get isCustom => type == TileType.custom;
  bool get isCollection => isRow || isColumn || isCustom;
  bool get isEmpty =>
      isCollection && tiles.isEmpty || isContent && content == null;

  double _width = 0;
  double get width => parent?.type == TileType.column ? _width * flex : _width;
  set width(double value) => _width = value;

  double _height = 0;
  double get height => parent?.type == TileType.row ? _height * flex : _height;
  set height(double value) => _height = value;

  void insert(int index, TileModel<T> tile) {
    tile.parent = this;
    tiles.insert(index, tile);
    notifyListeners();
  }

  void add(TileModel<T> tile) => insert(tiles.length, tile);

  void remove(TileModel<T> tile) {
    assert(tiles.contains(tile));
    tile.parent = null;
    tiles.remove(tile);
    notifyListeners();
  }

  void resize(double delta) {
    parent.type == TileType.column
        ? flex += flex / width * delta
        : flex += flex / height * delta;

    notifyListeners();
  }

  Size get minSize => isContent
      ? _minSize
      : tiles.map((tile) => tile.minSize).reduce(
          (s1, s2) => Size(max(s1.width, s2.width), max(s1.height, s2.height)));

  bool get overflowed => parent.type == TileType.column
      ? width < minSize.width
      : height < minSize.height;

  void notify() => notifyListeners();

  @override
  String toString() => isContent ? '$content' : '$type $tiles';
}
