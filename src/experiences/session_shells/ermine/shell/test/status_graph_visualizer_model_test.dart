// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// ignore_for_file: implementation_imports

import 'dart:math';

import 'package:ermine_library/src/widgets/status/status_graph_visualizer.dart';
import 'package:test/test.dart';

void main() {
  StatusGraphVisualizerModel testGraphModel;

  setUp(() async {
    testGraphModel = StatusGraphVisualizerModel();
  });

  test(
      'test to confirm StatusGraphVisualizerModel contains non-breaking default values',
      () {
    expect(testGraphModel.graphValue, 'loading...');
    expect(
        testGraphModel.graphData, lessThanOrEqualTo(testGraphModel.graphMax));
    expect(testGraphModel.graphData,
        greaterThanOrEqualTo(testGraphModel.graphMin));
    expect(testGraphModel.graphData, greaterThanOrEqualTo(0));
    expect(testGraphModel.graphHeight, greaterThan(0));
    expect(testGraphModel.graphWidth, greaterThan(0));
    expect(testGraphModel.graphMin, greaterThanOrEqualTo(0));
    expect(testGraphModel.graphMin, lessThan(testGraphModel.graphMax));
    expect(testGraphModel.graphMax, greaterThanOrEqualTo(0));
    expect(testGraphModel.graphMax, greaterThan(testGraphModel.graphMin));
    expect(testGraphModel.graphFirst, isNotNull);
    expect(testGraphModel.borderActive, isNotNull);
    expect(testGraphModel.fillActive, isNotNull);
  });

  test('test to confirm StatusGraphModel graphValue setter works properly', () {
    String initialBarValue = testGraphModel.graphValue;
    expect(testGraphModel.graphValue, initialBarValue);
    String testValue = 'testGraphValue';
    testGraphModel.graphValue = testValue;
    expect(testGraphModel.graphValue, testValue);
  });

  test('test to confirm StatusGraphModel graphData setter works properly', () {
    double initialBarMax = testGraphModel.graphData;
    expect(testGraphModel.graphData, initialBarMax);
    double randomGraphData = Random().nextDouble() * 100;
    testGraphModel.graphData = randomGraphData;
    expect(testGraphModel.graphData, randomGraphData);
  });

  tearDown(() async {
    testGraphModel = null;
  });
}
