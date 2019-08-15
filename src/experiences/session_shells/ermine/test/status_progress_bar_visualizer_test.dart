// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// ignore_for_file: implementation_imports

import 'dart:math';
import 'package:flutter/material.dart';

import 'package:ermine_library/src/widgets/status/status_progress_bar_visualizer.dart';
import 'package:test/test.dart';

void main() {
  StatusProgressBarVisualizer testProgressBar;
  StatusProgressBarVisualizer testProgressBarCustom;
  TextAlign testTextAlign;
  TextStyle testTextStyle;

  setUp(() async {
    testTextAlign = TextAlign.right;
    testTextStyle = TextStyle();
    testProgressBar = StatusProgressBarVisualizer(
      model: StatusProgressBarVisualizerModel(),
      textAlignment: testTextAlign,
      textStyle: testTextStyle,
    );
    testProgressBarCustom = StatusProgressBarVisualizer(
      model: StatusProgressBarVisualizerModel(),
      textAlignment: testTextAlign,
      textStyle: testTextStyle,
    );
  });

  test(
      'test to confirm StatusProgressBarVisualizer constructs via default model',
      () {
    expect(testProgressBar.model.barValue, 'loading...');
    expect(testProgressBar.model.barSize, inInclusiveRange(0, 1));
    expect(testProgressBar.model.barMax, greaterThan(0));
    expect(testProgressBar.model.barMax,
        greaterThanOrEqualTo(testProgressBar.model.barFill));
    expect(testProgressBar.model.barFill, greaterThanOrEqualTo(0));
    expect(testProgressBar.model.barFill,
        lessThanOrEqualTo(testProgressBar.model.barMax));
    expect(testProgressBar.model.offset, greaterThanOrEqualTo(0));
    expect(testProgressBar.model.barHeight, greaterThanOrEqualTo(0));
    expect(testProgressBar.model.barFirst, isNotNull);
  });

  test(
      'test to confirm StatusProgressBarVisualizer barValue is set correctly via model changes',
      () {
    // Confirm default barValue construction values are equal.
    expect(
        testProgressBar.model.barValue, testProgressBarCustom.model.barValue);
    // Change barValue within custom progress bar (which reflects in animation).
    String testCustomBarValue = 'test';
    testProgressBarCustom.model.barValue = testCustomBarValue;
    // Confirm barValue updated correctly & custom model has changed from default.
    expect(testProgressBarCustom.model.barValue, testCustomBarValue);
    expect(testProgressBar.model.barValue,
        isNot(testProgressBarCustom.model.barValue));
    // Change barValue value again to confirm continuous animation updates possible.
    testCustomBarValue = 'test2';
    testProgressBarCustom.model.barValue = testCustomBarValue;
    // Confirm barValue updated correctly again & custom model has changed from default.
    expect(testProgressBarCustom.model.barValue, testCustomBarValue);
    expect(testProgressBar.model.barValue,
        isNot(testProgressBarCustom.model.barValue));
  });

  test(
      'test to confirm StatusProgressBarVisualizer barFill is set correctly via model changes',
      () {
    // Confirm default barFill construction values are equal.
    expect(testProgressBar.model.barFill, testProgressBarCustom.model.barFill);
    // Change barFill within custom progress bar (which reflects in animation).
    double testRandomBarFill = Random().nextDouble() * 100;
    testProgressBarCustom.model.barFill = testRandomBarFill;
    // Confirm barFill updated correctly & custom model has changed from default.
    expect(testProgressBarCustom.model.barFill, testRandomBarFill);
    expect(testProgressBar.model.barFill,
        isNot(testProgressBarCustom.model.barFill));
    // Change barFill value again to confirm continuous animation updates possible.
    testRandomBarFill = Random().nextDouble() * 100;
    testProgressBarCustom.model.barFill = testRandomBarFill;
    // Confirm barFill updated correctly again & custom model has changed from default.
    expect(testProgressBarCustom.model.barFill, testRandomBarFill);
    expect(testProgressBar.model.barFill,
        isNot(testProgressBarCustom.model.barFill));
  });

  test(
      'test to confirm StatusProgressBarVisualizer barMax is set correctly via model changes',
      () {
    // Confirm default barMax construction values are equal.
    expect(testProgressBar.model.barMax, testProgressBarCustom.model.barMax);
    // Change barMax within custom progress bar (which reflects in animation).
    double testRandomBarMax = Random().nextDouble() * 100;
    testProgressBarCustom.model.barMax = testRandomBarMax;
    // Confirm barMax updated correctly & custom model has changed from default.
    expect(testProgressBarCustom.model.barMax, testRandomBarMax);
    expect(testProgressBar.model.barMax,
        isNot(testProgressBarCustom.model.barMax));
    // Change barMax value again to confirm continuous animation updates possible.
    testRandomBarMax = Random().nextDouble() * 100;
    testProgressBarCustom.model.barMax = testRandomBarMax;
    // Confirm barMax updated correctly again & custom model has changed from default.
    expect(testProgressBarCustom.model.barMax, testRandomBarMax);
    expect(testProgressBar.model.barMax,
        isNot(testProgressBarCustom.model.barMax));
  });

  tearDown(() async {
    testProgressBar = null;
    testProgressBarCustom = null;
    testTextAlign = null;
    testTextStyle = null;
  });
}
