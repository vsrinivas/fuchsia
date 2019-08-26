// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// ignore_for_file: implementation_imports

import 'dart:math';
import 'package:flutter/material.dart';

import 'package:ermine_library/src/widgets/status/status_graph_visualizer.dart';
import 'package:test/test.dart';

void main() {
  StatusGraphVisualizer testGraph;
  StatusGraphVisualizer testGraphCustom;
  MainAxisAlignment testAlign;
  TextStyle testTextStyle;
  Paint testPaint;

  setUp(() async {
    testAlign = MainAxisAlignment.spaceBetween;
    testTextStyle = TextStyle();
    testPaint = Paint();
    testGraph = StatusGraphVisualizer(
      model: StatusGraphVisualizerModel(),
      axisAlignment: testAlign,
      textStyle: testTextStyle,
      drawStyle: testPaint,
    );
    testGraphCustom = StatusGraphVisualizer(
      model: StatusGraphVisualizerModel(),
      axisAlignment: testAlign,
      textStyle: testTextStyle,
      drawStyle: testPaint,
    );
  });

  test('test to confirm StatusGraphVisualizer constructs via default model',
      () {
    expect(testGraph.model.graphValue, 'loading...');
    expect(
        testGraph.model.graphData, lessThanOrEqualTo(testGraph.model.graphMax));
    expect(testGraph.model.graphData,
        greaterThanOrEqualTo(testGraph.model.graphMin));
    expect(testGraph.model.graphData, greaterThanOrEqualTo(0));
    expect(testGraph.model.graphHeight, greaterThan(0));
    expect(testGraph.model.graphWidth, greaterThan(0));
    expect(testGraph.model.graphMin, greaterThanOrEqualTo(0));
    expect(testGraph.model.graphMin, lessThan(testGraph.model.graphMax));
    expect(testGraph.model.graphMax, greaterThanOrEqualTo(0));
    expect(testGraph.model.graphMax, greaterThan(testGraph.model.graphMin));
    expect(testGraph.model.graphFirst, isNotNull);
    expect(testGraph.model.borderActive, isNotNull);
    expect(testGraph.model.fillActive, isNotNull);
  });

  test(
      'test to confirm StatusGraphVisualizer graphValue is set correctly via model changes',
      () {
    // Confirm default graphValue construction values are equal.
    expect(testGraph.model.graphValue, testGraphCustom.model.graphValue);
    // Change graphValue within custom graph (which reflects in animation).
    String testCustomGraphValue = 'test';
    testGraphCustom.model.graphValue = testCustomGraphValue;
    // Confirm graphValue updated correctly & custom model has changed from default.
    expect(testGraphCustom.model.graphValue, testCustomGraphValue);
    expect(testGraph.model.graphValue, isNot(testGraphCustom.model.graphValue));
    // Change graphValue value again to confirm continuous animation updates possible.
    testCustomGraphValue = 'test2';
    testGraphCustom.model.graphValue = testCustomGraphValue;
    // Confirm graphValue updated correctly again & custom model has changed from default.
    expect(testGraphCustom.model.graphValue, testCustomGraphValue);
    expect(testGraph.model.graphValue, isNot(testGraphCustom.model.graphValue));
  });

  test(
      'test to confirm StatusGraphVisualizer graphData is set correctly via model changes',
      () {
    // Confirm default graphData construction values are equal.
    expect(testGraph.model.graphData, testGraphCustom.model.graphData);
    // Change graphData within custom graph (which reflects in animation).
    double testRandomGraphData = Random().nextDouble() * 100;
    testGraphCustom.model.graphData = testRandomGraphData;
    // Confirm graphData updated correctly & custom model has changed from default.
    expect(testGraphCustom.model.graphData, testRandomGraphData);
    expect(testGraph.model.graphData, isNot(testGraphCustom.model.graphData));
    // Change graphData value again to confirm continuous animation updates possible.
    testRandomGraphData = Random().nextDouble() * 100;
    testGraphCustom.model.graphData = testRandomGraphData;
    // Confirm graphData updated correctly again & custom model has changed from default.
    expect(testGraphCustom.model.graphData, testRandomGraphData);
    expect(testGraph.model.graphData, isNot(testGraphCustom.model.graphData));
  });

  tearDown(() async {
    testGraph = null;
    testGraphCustom = null;
    testAlign = null;
    testTextStyle = null;
    testPaint = null;
  });
}
