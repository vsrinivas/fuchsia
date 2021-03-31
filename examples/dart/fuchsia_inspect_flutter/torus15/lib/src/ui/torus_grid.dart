// 15 Puzzle on Torus - June 2019
// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:math';

import 'package:flutter/material.dart';
import 'package:fuchsia_inspect/inspect.dart' as inspect;

import '../logic/torus_logic.dart';
import 'torus_tile.dart';

enum _Direction { none, right, down, left, up }

class TorusGrid extends StatefulWidget {
  const TorusGrid({@required this.cols, @required this.rows, this.inspectNode});

  final inspect.Node inspectNode;
  final int cols;
  final int rows;

  @override
  TorusGridState createState() => TorusGridState();
}

class TorusGridState extends State<TorusGrid> {
  TorusPuzzle torusPuzzle;

  static const double _minPanVelocity = 15.0;

  // not in constructor because widget property must be initalized
  @override
  void initState() {
    super.initState();

    torusPuzzle = TorusPuzzle(widget.cols, widget.rows, widget.inspectNode)
      ..shufflePuzzle();
  }

  _Direction velocityDirection(Velocity velocity) {
    var absDx = velocity.pixelsPerSecond.dx.abs();
    var absDy = velocity.pixelsPerSecond.dy.abs();
    if (max(absDx, absDy) < _minPanVelocity) {
      return _Direction.none;
    }

    if (absDx > absDy) {
      return velocity.pixelsPerSecond.dx > 0
          ? _Direction.right
          : _Direction.left;
    } else {
      return velocity.pixelsPerSecond.dy > 0 ? _Direction.down : _Direction.up;
    }
  }

  // create swipe gesture handler to move tile
  GestureDragEndCallback tileMoveFactory(int col, int row) {
    return (details) {
      setState(() {
        _Direction tileDragDirection = velocityDirection(details.velocity);
        switch (tileDragDirection) {
          case _Direction.right:
            torusPuzzle.rotateRow(row, rotateRight: true);
            break;
          case _Direction.left:
            torusPuzzle.rotateRow(row, rotateRight: false);
            break;
          case _Direction.down:
            torusPuzzle.rotateCol(col, rotateDown: true);
            break;
          case _Direction.up:
            torusPuzzle.rotateCol(col, rotateDown: false);
            break;
          case _Direction.none:
            // do nothing
            break;
        }
      });
    };
  }

  // create grid of tiles
  // GridView Widget is for scrolling Widget views and not used here
  @override
  Widget build(BuildContext context) {
    return Directionality(
      textDirection: TextDirection.ltr,
      child: Material(
        child: Column(
          mainAxisAlignment: MainAxisAlignment.center,
          children: List<Widget>.generate(widget.rows, (int row) {
            return Row(
              mainAxisAlignment: MainAxisAlignment.center,
              children: List<Widget>.generate(widget.cols, (int col) {
                return TorusTile(
                  number: torusPuzzle.tiles[torusPuzzle.tileId(col, row)],
                  tileMoveHandler: tileMoveFactory(col, row),
                );
              }),
            ); // Row
          }),
        ), // Column
      ), // Material
    );
  }
}
