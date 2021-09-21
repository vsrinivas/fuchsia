// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TODO(https://fxbug.dev/84961): Fix null safety and remove this language version.
// @dart=2.9

// ignore_for_file: implementation_imports
import 'package:fuchsia_inspect/inspect.dart' as inspect;
import 'package:mockito/mockito.dart';
import 'package:test/test.dart';
import 'package:torus15/src/logic/torus_logic.dart';

class MockNode extends Mock implements inspect.Node {}

void main() {
  group('Torus Puzzle Logic:', () {
    const testPuzzleRows = 3;
    const testPuzzleCols = 3;

    test('Generation Test', () {
      var testPuzzle = TorusPuzzle(testPuzzleRows, testPuzzleCols);
      var expectedPuzzle = TorusPuzzle.from('0 1 2 '
          '3 4 5 '
          '6 7 8');
      expect(testPuzzle, expectedPuzzle);
    });

    test('Row Rotate', () {
      var testPuzzle = TorusPuzzle(testPuzzleRows, testPuzzleCols);
      var expectedPuzzle = TorusPuzzle.from('0 1 2 '
          '4 5 3 '
          '8 6 7');
      testPuzzle
        ..rotateRow(1, rotateRight: false)
        ..rotateRow(2, rotateRight: true);
      expect(testPuzzle, expectedPuzzle);
    });

    test('Column Rotate', () {
      var testPuzzle = TorusPuzzle(testPuzzleRows, testPuzzleCols);
      var expectedPuzzle = TorusPuzzle.from('6 4 2 '
          '0 7 5 '
          '3 1 8');
      testPuzzle
        ..rotateCol(0, rotateDown: true)
        ..rotateCol(1, rotateDown: false);
      expect(testPuzzle, expectedPuzzle);
    });

    test('Rotation Composition', () {
      var testPuzzle = TorusPuzzle(testPuzzleRows, testPuzzleCols);
      var expectedPuzzle = TorusPuzzle.from('3 7 1 '
          '4 0 5 '
          '2 8 6');
      testPuzzle
        ..rotateRow(0, rotateRight: true)
        ..rotateCol(1, rotateDown: true)
        ..rotateRow(2, rotateRight: false)
        ..rotateCol(0, rotateDown: false);
      expect(testPuzzle, expectedPuzzle);
    });
  });

  group('Inspect Integration:', () {
    const testPuzzleRows = 4;
    const testPuzzleCols = 4;
    const testPuzzleTilec = testPuzzleRows * testPuzzleCols;

    test('Generation Test', () {
      var mockNode = MockNode(); // fake inspect node
      var testPuzzle = TorusPuzzle(testPuzzleRows, testPuzzleCols, mockNode);

      // satisfy dart unused_local_variable
      clearInteractions(mockNode);
      testPuzzle.resetPuzzle();

      // each tile's inspect data should be written
      for (int i = 0; i < testPuzzleTilec; i++) {
        verify(mockNode.intProperty('$i')).called(1);
      }
    });

    test('Row Rotate', () {
      var mockNode = MockNode();
      var testPuzzle = TorusPuzzle(testPuzzleRows, testPuzzleCols, mockNode);

      clearInteractions(mockNode);
      testPuzzle.rotateRow(0, rotateRight: true);
      // only first row should update inspect info
      for (int i = 0; i < testPuzzleTilec; i++) {
        if (i < testPuzzleCols) {
          verify(mockNode.intProperty('$i')).called(1);
        } else {
          verifyNever(mockNode.intProperty('$i'));
        }
      }
    });

    test('Column Rotate', () {
      var mockNode = MockNode();
      var testPuzzle = TorusPuzzle(testPuzzleRows, testPuzzleCols, mockNode);

      clearInteractions(mockNode);
      testPuzzle.rotateCol(0, rotateDown: true);
      for (int i = 0; i < testPuzzleTilec; i++) {
        if (i % testPuzzleRows == 0) {
          verify(mockNode.intProperty('$i')).called(1);
        } else {
          verifyNever(mockNode.intProperty('$i'));
        }
      }
    });
  });
}
