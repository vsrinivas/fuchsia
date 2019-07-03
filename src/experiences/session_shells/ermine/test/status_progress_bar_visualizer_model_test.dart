// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// ignore_for_file: implementation_imports

import 'dart:math';

import 'package:ermine_library/src/widgets/status_progress_bar_visualizer.dart';
import 'package:test/test.dart';

void main() {
  test(
      'test to confirm StatusProgressBarVisualizerModel contains non-breaking default values',
      () {
    StatusProgressBarVisualizerModel testProgressBarModel =
        StatusProgressBarVisualizerModel();
    expect(testProgressBarModel.barValue, 'loading...');
    expect(testProgressBarModel.barSize, inInclusiveRange(0, 1));
    expect(testProgressBarModel.barMax, greaterThan(0));
    expect(testProgressBarModel.barMax,
        greaterThanOrEqualTo(testProgressBarModel.barFill));
    expect(testProgressBarModel.barFill, greaterThanOrEqualTo(0));
    expect(testProgressBarModel.barFill,
        lessThanOrEqualTo(testProgressBarModel.barMax));
    expect(testProgressBarModel.offset, greaterThanOrEqualTo(0));
    expect(testProgressBarModel.barHeight, greaterThanOrEqualTo(0));
    expect(testProgressBarModel.barFirst, isNotNull);
  });

  test('test to confirm StatusProgressBarVisualizerModel setters work properly',
      () {
    StatusProgressBarVisualizerModel testProgressBarModel =
        StatusProgressBarVisualizerModel();
    // Check setter for barValue
    String initialBarValue = testProgressBarModel.barValue;
    expect(testProgressBarModel.barValue, initialBarValue);
    String testValue = 'testBarValue';
    testProgressBarModel.barValue = testValue;
    expect(testProgressBarModel.barValue, testValue);
    // Check setter for barFill
    double initialBarFill = testProgressBarModel.barFill;
    expect(testProgressBarModel.barFill, initialBarFill);
    double randomBarFill = Random().nextDouble() * 100;
    testProgressBarModel.barFill = randomBarFill;
    expect(testProgressBarModel.barFill, randomBarFill);
    // Check setter for barMax
    double initialBarMax = testProgressBarModel.barMax;
    expect(testProgressBarModel.barMax, initialBarMax);
    double randomBarMax = Random().nextDouble() * 100;
    testProgressBarModel.barMax = randomBarMax;
    expect(testProgressBarModel.barMax, randomBarMax);
  });
}
