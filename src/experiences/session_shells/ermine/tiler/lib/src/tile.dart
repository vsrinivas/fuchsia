// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:flutter/material.dart';

import 'sizer.dart';
import 'tile_model.dart';
import 'utils.dart';

typedef TileChromeBuilder<T> = Widget Function(
    BuildContext context, TileModel<T> tile);
typedef CustomTilesBuilder<T> = Widget Function(
    BuildContext context, List<TileModel<T>> tiles);

/// A [Widget] that renders a tile give its [TileModel]. If the tile type is
/// [TileType.content], it calls [chromeBuilder] to build a widget to render
/// the tile. Otherwise, it renders the [tiles] children in row or column
/// order. It calls [sizerBuilder] to get a sizing widget to display between
/// rows or columns of tiles.
class Tile<T> extends StatelessWidget {
  final TileModel<T> model;
  final TileChromeBuilder<T> chromeBuilder;
  final TileSizerBuilder sizerBuilder;
  final CustomTilesBuilder<T> customTilesBuilder;
  final double sizerThickness;
  final ValueChanged<TileModel<T>> onFloat;

  const Tile({
    @required this.model,
    @required this.chromeBuilder,
    this.customTilesBuilder,
    this.sizerBuilder,
    this.sizerThickness,
    this.onFloat,
  });

  @override
  Widget build(BuildContext context) {
    return AnimatedBuilder(
      animation: model,
      builder: (context, child) {
        final type = model.type;
        if (model.isContent) {
          return chromeBuilder(context, model);
        } else if (model.tiles.isEmpty) {
          return SizedBox.shrink();
        } else {
          return LayoutBuilder(
            builder: (context, constraints) {
              assert(constraints.isTight);

              // Filter out tiles that are floating.
              final availableTiles = model.tiles;

              /// Filter out tiles that don't fit based on their minSize.
              final availableSize =
                  _availableSize(type, availableTiles, constraints.biggest);
              final tiles = _fit(type, availableTiles, availableSize).toList();
              bool fit = tiles.length == availableTiles.length;

              if (type != TileType.custom &&
                  (fit || customTilesBuilder == null)) {
                // Float the tiles that did not fit.
                availableTiles
                    .where((t) => !tiles.contains(t))
                    .toList()
                    .forEach(onFloat?.call);

                // All tiles fit, set flex,width and height and return tiles.
                final flex = _totalFlex(tiles);
                for (var tile in tiles) {
                  tile
                    ..flex = tile.flex / flex
                    ..width = availableSize.width
                    ..height = availableSize.height;
                }

                // Generate tile widgets with sizer widgets interleaved.
                final count = tiles.length + tiles.length - 1;
                final tileWidgets = List<Widget>.generate(count, (index) {
                  if (index.isOdd) {
                    return Sizer(
                      direction: model.type == TileType.row
                          ? Axis.horizontal
                          : Axis.vertical,
                      tileBefore: tiles[index ~/ 2],
                      tileAfter: tiles[index ~/ 2 + 1],
                      sizerBuilder: sizerBuilder,
                    );
                  } else {
                    return _tileBuilder(
                      tile: tiles[index ~/ 2],
                      chromeBuilder: chromeBuilder,
                      sizerBuilder: sizerBuilder,
                      customTilesBuilder: customTilesBuilder,
                      sizerThickness: sizerThickness,
                      onFloat: onFloat,
                    );
                  }
                });

                return model.type == TileType.row
                    ? Column(
                        crossAxisAlignment: CrossAxisAlignment.stretch,
                        children: tileWidgets,
                      )
                    : Row(
                        crossAxisAlignment: CrossAxisAlignment.stretch,
                        children: tileWidgets,
                      );
              } else {
                // Flatten all tiles to group them for tabbing between them.
                final allTiles = flatten<T>(availableTiles).toList();
                // Display tabs only when we have more than 1 tile.
                if (allTiles.length > 1) {
                  return customTilesBuilder?.call(context, allTiles) ??
                      Offstage();
                } else {
                  return _tileBuilder(
                      tile: allTiles.first,
                      chromeBuilder: chromeBuilder,
                      sizerBuilder: sizerBuilder,
                      customTilesBuilder: customTilesBuilder,
                      sizerThickness: sizerThickness,
                      onFloat: onFloat);
                }
              }
            },
          );
        }
      },
    );
  }

  Widget _tileBuilder({
    TileModel<T> tile,
    TileChromeBuilder<T> chromeBuilder,
    TileSizerBuilder sizerBuilder,
    CustomTilesBuilder<T> customTilesBuilder,
    ValueChanged<TileModel<T>> onFloat,
    double sizerThickness,
  }) =>
      AnimatedBuilder(
        animation: tile,
        child: Tile(
          model: tile,
          chromeBuilder: chromeBuilder,
          sizerBuilder: sizerBuilder,
          customTilesBuilder: customTilesBuilder,
          onFloat: onFloat,
          sizerThickness: sizerThickness,
        ),
        builder: (context, child) => SizedBox(
              width: tile.width,
              height: tile.height,
              child: child,
            ),
      );

  /// Fit as many tiles as possible within [size].
  Iterable<TileModel<T>> _fit(
      TileType type, Iterable<TileModel<T>> tiles, Size size) {
    if (tiles.isEmpty) {
      return tiles;
    }

    // Calculate the normalized flex of each tile.
    final flex = _totalFlex(tiles);
    final width = size.width;
    final height = size.height;

    // Find tiles that don't fit based on their minSize.
    final unfitableTiles = tiles.where((tile) {
      double tileFlex = tile.flex / flex;
      double tileWidth = type == TileType.column ? tileFlex * width : width;
      double tileHeight = type == TileType.row ? tileFlex * height : height;
      return type == TileType.column
          ? tile.minSize.width > tileWidth
          : tile.minSize.height > tileHeight;
    });

    if (unfitableTiles.isEmpty) {
      return tiles;
    } else {
      // Remove the last tile in unfittableTiles from tiles and run fit again.
      return _fit(type, tiles.where((t) => t != unfitableTiles.last), size);
    }
  }

  // Returns the size available after sizers thickness is accounted for.
  Size _availableSize(TileType type, Iterable<TileModel<T>> tiles, Size size) {
    final numTiles = tiles.length;
    final numSizers = numTiles - 1;

    // Calculate the width/height of a tile minus sizer thickness.
    final width = type == TileType.column
        ? (size.width - sizerThickness * numSizers)
        : size.width;
    final height = type == TileType.row
        ? (size.height - sizerThickness * numSizers)
        : size.height;
    return Size(width, height);
  }

  double _totalFlex(Iterable<TileModel<T>> tiles) =>
      tiles.map((t) => t.flex).reduce((f1, f2) => f1 + f2);
}
