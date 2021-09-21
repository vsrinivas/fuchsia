// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TODO(https://fxbug.dev/84961): Fix null safety and remove this language version.
// @dart=2.9

import 'package:collection/collection.dart' show ListEquality;
import 'package:fuchsia_inspect/inspect.dart' as inspect;

class TorusPuzzle {
  TorusPuzzle(this.cols, this.rows, [this.inspectNode])
      : _tileCount = cols * rows {
    resetPuzzle();
  }

  // TorusPuzzle requires row/col data to be mutable
  // use constructor for creating puzzles to test against
  TorusPuzzle.from(String str)
      : cols = -1,
        rows = -1,
        _tileCount = -1 {
    var strTiles = str.split(' ');
    tiles = List<int>.generate(strTiles.length, (i) => int.parse(strTiles[i]));
    _inspectPuzzle();
  }

  inspect.Node inspectNode;
  final int cols;
  final int rows;
  final int _tileCount;

  List<int> tiles;

  static const int _rotateForwardStep = -1;
  static const int _rotateBackwardStep = 1;

  // two TorusPuzzles are equal if their tiles are in the same positions
  @override
  bool operator ==(Object o) {
    return o is TorusPuzzle && ListEquality().equals(tiles, o.tiles);
  }

  @override
  int get hashCode => tiles.hashCode;

  // col and row are currently (June 2019) always in bounds
  int tileId(int col, int row) {
    return row * cols + col;
  }

  // put puzzle in solved state (for all i tiles[i] == i)
  void resetPuzzle() {
    tiles = List<int>.generate(_tileCount, (int i) => i);
    _inspectPuzzle();
  }

  // place tiles in random locations (all permutations are legal)
  void shufflePuzzle() {
    tiles.shuffle();
  }

  // publishes torus puzzle tiles to component inspection 'tile' node
  void _inspectPuzzle() {
    for (int i = 0; i < tiles.length; i++) {
      inspectNode?.intProperty('$i')?.setValue(tiles[i]);
    }
  }

  void updateTile(int col, int row, int value) {
    int writeTileId = tileId(col, row);
    tiles[writeTileId] = value;
    inspectNode?.intProperty('$writeTileId')?.setValue(value);
  }

  void rotateRow(int row, {bool rotateRight = true}) {
    int col = rotateRight ? cols - 1 : 0; // the column to read from
    int step = rotateRight ? _rotateForwardStep : _rotateBackwardStep;
    int firstTile = tiles[tileId(col, row)];
    do {
      updateTile(col, row, tiles[tileId(col + step, row)]);
      col += step;
    } while (col > 0 && col < cols - 1);
    updateTile(col, row, firstTile);
  }

  void rotateCol(int col, {bool rotateDown = true}) {
    int row = rotateDown ? rows - 1 : 0; // the row to read from
    int step = rotateDown ? _rotateForwardStep : _rotateBackwardStep;
    int firstTile = tiles[tileId(col, row)];
    do {
      updateTile(col, row, tiles[tileId(col, row + step)]);
      row += step;
    } while (row > 0 && row < rows - 1);
    updateTile(col, row, firstTile);
  }
}
