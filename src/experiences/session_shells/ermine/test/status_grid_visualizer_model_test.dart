// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// ignore_for_file: implementation_imports

//import 'dart:math';

import 'package:ermine_library/src/widgets/status_grid_visualizer.dart';
import 'package:test/test.dart';

void main() {
  StatusGridVisualizerModel testGridModel;

  setUp(() async {
    testGridModel = StatusGridVisualizerModel();
  });

  test(
      'test to confirm StatusGridVisualizerModel contains non-breaking default values',
      () {
    expect(testGridModel.gridHeaders.split(',').length, testGridModel.gridRows);
    expect(testGridModel.gridValues.split(',').length,
        testGridModel.gridColumns * testGridModel.gridRows);
    expect(
        testGridModel.gridTitles.split(',').length, testGridModel.gridColumns);
    expect(testGridModel.gridColumns, greaterThanOrEqualTo(1));
    expect(testGridModel.gridRows, greaterThanOrEqualTo(1));
    expect(testGridModel.gridHeaderTitle, 'Name');
    expect(testGridModel.gridIndent, greaterThanOrEqualTo(0));
    expect(testGridModel.gridDataOffset, greaterThanOrEqualTo(0));
    expect(testGridModel.gridColumnSpace, greaterThanOrEqualTo(0));
    expect(testGridModel.gridHeight, greaterThan(0));
  });

  test(
      'test to confirm StatusGridVisualizerModel gridValues setter works properly',
      () {
    String initialGridValues = testGridModel.gridValues;
    expect(testGridModel.gridValues, initialGridValues);
    String testValues = 'testGridValue,testGridValue,testGridValue';
    testGridModel.gridValues = testValues;
    expect(testGridModel.gridValues, testValues);
  });

  tearDown(() async {
    testGridModel = null;
  });
}
