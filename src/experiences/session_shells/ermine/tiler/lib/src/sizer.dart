// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:flutter/material.dart';

import 'tile_model.dart';

typedef TileSizerBuilder = Widget Function(
  BuildContext context,
  Axis direction,
  TileModel tileBefore,
  TileModel tileAfter,
);

class Sizer extends StatelessWidget {
  final Axis direction;
  final TileSizerBuilder sizerBuilder;
  final TileModel tileBefore;
  final TileModel tileAfter;
  final bool horizontal;

  const Sizer({
    this.direction,
    this.tileBefore,
    this.tileAfter,
    this.sizerBuilder,
  }) : horizontal = direction == Axis.horizontal;

  @override
  Widget build(BuildContext context) {
    final sizer = sizerBuilder?.call(context, direction, tileBefore, tileAfter);
    if (sizer == null) {
      return SizedBox.shrink();
    } else {
      return Listener(
        onPointerMove: _onPointerMove,
        child: Container(
          color: Colors.transparent,
          child: sizer,
        ),
      );
    }
  }

  void _onPointerMove(PointerMoveEvent event) {
    double delta =
        direction == Axis.horizontal ? event.delta.dy : event.delta.dx;
    tileBefore.resize(delta);
    tileAfter.resize(-delta);
    if (tileBefore.overflowed || tileAfter.overflowed) {
      tileBefore.resize(-delta);
      tileAfter.resize(delta);
    }
  }
}
