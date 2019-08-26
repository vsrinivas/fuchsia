// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// ignore_for_file: implementation_imports

import 'package:flutter/material.dart';

import 'package:ermine_library/src/widgets/status/status_grid_visualizer.dart';
import 'package:test/test.dart';

void main() {
  StatusGridVisualizer testGrid;
  StatusGridVisualizer testGridCustom;
  TextStyle testTextStyle;
  Text testTitle;
  double testTitleHeight;

  setUp(() async {
    testTextStyle = TextStyle();
    testTitle = Text('title');
    testTitleHeight = 24.0;
    testGrid = StatusGridVisualizer(
      model: StatusGridVisualizerModel(),
      textStyle: testTextStyle,
      title: testTitle,
      titleHeight: testTitleHeight,
    );
    testGridCustom = StatusGridVisualizer(
      model: StatusGridVisualizerModel(),
      textStyle: testTextStyle,
      title: testTitle,
      titleHeight: testTitleHeight,
    );
  });

  test('test to confirm StatusGridVisualizer constructs via default model', () {
    expect(
        testGrid.model.gridHeaders.split(',').length, testGrid.model.gridRows);
    expect(testGrid.model.gridValues.split(',').length,
        testGrid.model.gridColumns * testGrid.model.gridRows);
    expect(testGrid.model.gridTitles.split(',').length,
        testGrid.model.gridColumns);
    expect(testGrid.model.gridColumns, greaterThanOrEqualTo(1));
    expect(testGrid.model.gridRows, greaterThanOrEqualTo(1));
    expect(testGrid.model.gridHeaderTitle, 'Name');
    expect(testGrid.model.gridIndent, greaterThanOrEqualTo(0));
    expect(testGrid.model.gridDataOffset, greaterThanOrEqualTo(0));
    expect(testGrid.model.gridColumnSpace, greaterThanOrEqualTo(0));
    expect(testGrid.model.gridHeight, greaterThan(0));
  });

  test(
      'test to confirm StatusGridVisualizer gridValues is set correctly via model changes',
      () {
    // Confirm default gridValues construction values are equal.
    expect(testGrid.model.gridValues, testGridCustom.model.gridValues);
    // Change gridValues within custom grid (which reflects in animation).
    String testCustomGridValue = 'test,test,test';
    testGridCustom.model.gridValues = testCustomGridValue;
    // Confirm gridValues updated correctly & custom model has changed from default.
    expect(testGridCustom.model.gridValues, testCustomGridValue);
    expect(testGrid.model.gridValues, isNot(testGridCustom.model.gridValues));
    // Change gridValues value again to confirm continuous animation updates possible.
    testCustomGridValue = 'test2,test2,test2';
    testGridCustom.model.gridValues = testCustomGridValue;
    // Confirm gridValues updated correctly again & custom model has changed from default.
    expect(testGridCustom.model.gridValues, testCustomGridValue);
    expect(testGrid.model.gridValues, isNot(testGridCustom.model.gridValues));
  });

  tearDown(() async {
    testGrid = null;
    testGridCustom = null;
    testTextStyle = null;
    testTitle = null;
    testTitleHeight = null;
  });
}
