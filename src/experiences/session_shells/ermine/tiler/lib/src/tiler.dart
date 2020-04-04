// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:math';
import 'package:flutter/material.dart';

import 'sizer.dart';
import 'tile.dart';
import 'tile_model.dart';
import 'tiler_model.dart';

/// Defines a widget to arrange tiles supplied in [model]. For tiles that are
/// leaf nodes in the [model], it calls the [chromeBuilder] to build their
/// widget. The [sizerBuilder] is called to display a sizing widget between
/// two tiles. If [sizerBuilder] returns null, no space is created between
/// the tiles. The supplied [sizerThickness] is used during layout calculations.
class Tiler<T> extends StatelessWidget {
  final TilerModel<T> model;
  final TileChromeBuilder<T> chromeBuilder;
  final TileSizerBuilder sizerBuilder;
  final CustomTilesBuilder<T> customTilesBuilder;
  final double sizerThickness;
  final ValueChanged<TileModel<T>> onFloat;

  const Tiler({
    @required this.model,
    @required this.chromeBuilder,
    this.customTilesBuilder,
    this.sizerBuilder,
    this.sizerThickness = 0,
    this.onFloat,
  });

  @override
  Widget build(BuildContext context) {
    return AnimatedBuilder(
      animation: model,
      builder: (_, __) {
        return model.root != null
            ? Stack(
                children: <Widget>[
                  Positioned.fill(
                    child: Tile<T>(
                      model: model.root,
                      chromeBuilder: chromeBuilder,
                      sizerBuilder: sizerBuilder,
                      customTilesBuilder: customTilesBuilder,
                      sizerThickness: sizerThickness,
                      onFloat: onFloat ?? model.floatTile,
                    ),
                  ),
                  Positioned.fill(
                    child: LayoutBuilder(
                      builder: (context, constraint) {
                        Offset offset = Offset.zero;
                        List<Widget> children = [];
                        for (final tile in model.floats) {
                          tile
                            ..position ??= offset
                            ..width = max(tile.minSize.width, 300)
                            ..height = max(tile.minSize.height, 300);
                          final widget = Positioned(
                            left: tile.position.dx,
                            top: tile.position.dy,
                            child: SizedBox(
                              width: tile.width,
                              height: tile.height,
                              child: Tile<T>(
                                model: tile,
                                chromeBuilder: chromeBuilder,
                                sizerBuilder: sizerBuilder,
                                customTilesBuilder: customTilesBuilder,
                                sizerThickness: sizerThickness,
                                onFloat: model.floatTile,
                              ),
                            ),
                          );
                          children.add(widget);
                          offset += Offset(20, 20);
                        }
                        return Stack(
                          children: children,
                        );
                      },
                    ),
                  ),
                ],
              )
            : Offstage();
      },
    );
  }
}
