// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// ignore_for_file: implementation_imports

import 'dart:math';
import 'package:flutter/material.dart';

import 'package:ermine_library/src/widgets/status/status_tick_bar_visualizer.dart';
import 'package:test/test.dart';

void main() {
  StatusTickBarVisualizer testTickBar;
  StatusTickBarVisualizer testTickBarCustom;
  TextAlign testTextAlign;
  TextStyle testTextStyle;

  setUp(() async {
    testTextAlign = TextAlign.right;
    testTextStyle = TextStyle();
    testTickBar = StatusTickBarVisualizer(
      model: StatusTickBarVisualizerModel(),
      textAlignment: testTextAlign,
      textStyle: testTextStyle,
    );
    testTickBarCustom = StatusTickBarVisualizer(
      model: StatusTickBarVisualizerModel(),
      textAlignment: testTextAlign,
      textStyle: testTextStyle,
    );
  });

  test('test to confirm StatusTickBarVisualizer constructs via default model',
      () {
    expect(testTickBar.model.barValue, 'loading...');
    expect(testTickBar.model.barFill, greaterThanOrEqualTo(0));
    expect(
        testTickBar.model.barFill, lessThanOrEqualTo(testTickBar.model.barMax));
    expect(testTickBar.model.barMax, greaterThanOrEqualTo(0));
    expect(testTickBar.model.barMax,
        greaterThanOrEqualTo(testTickBar.model.barFill));
    expect(testTickBar.model.tickMax, greaterThan(0));
    expect(testTickBar.model.barFirst, isNotNull);
  });

  test(
      'test to confirm StatusTickBarVisualizer barValue is set correctly via model changes',
      () {
    // Confirm default barValue construction values are equal.
    expect(testTickBar.model.barValue, testTickBarCustom.model.barValue);
    // Change barValue within custom tick bar (which reflects in animation).
    String testCustomBarValue = 'test';
    testTickBarCustom.model.barValue = testCustomBarValue;
    // Confirm barValue updated correctly & custom model has changed from default.
    expect(testTickBarCustom.model.barValue, testCustomBarValue);
    expect(testTickBar.model.barValue, isNot(testTickBarCustom.model.barValue));
    // Change barValue value again to confirm continuous animation updates possible.
    testCustomBarValue = 'test2';
    testTickBarCustom.model.barValue = testCustomBarValue;
    // Confirm barValue updated correctly again & custom model has changed from default.
    expect(testTickBarCustom.model.barValue, testCustomBarValue);
    expect(testTickBar.model.barValue, isNot(testTickBarCustom.model.barValue));
  });

  test(
      'test to confirm StatusTickBarVisualizer barFill is set correctly via model changes',
      () {
    // Confirm default barFill construction values are equal.
    expect(testTickBar.model.barFill, testTickBarCustom.model.barFill);
    // Change barFill within custom tick bar (which reflects in animation).
    double testRandomBarFill = Random().nextDouble() * 100;
    testTickBarCustom.model.barFill = testRandomBarFill;
    // Confirm barFill updated correctly & custom model has changed from default.
    expect(testTickBarCustom.model.barFill, testRandomBarFill);
    expect(testTickBar.model.barFill, isNot(testTickBarCustom.model.barFill));
    // Change barFill value again to confirm continuous animation updates possible.
    testRandomBarFill = Random().nextDouble() * 100;
    testTickBarCustom.model.barFill = testRandomBarFill;
    // Confirm barFill updated correctly again & custom model has changed from default.
    expect(testTickBarCustom.model.barFill, testRandomBarFill);
    expect(testTickBar.model.barFill, isNot(testTickBarCustom.model.barFill));
  });

  test(
      'test to confirm StatusTickBarVisualizer barMax is set correctly via model changes',
      () {
    // Confirm default barMax construction values are equal.
    expect(testTickBar.model.barMax, testTickBarCustom.model.barMax);
    // Change barMax within custom tick bar (which reflects in animation).
    double testRandomBarMax = Random().nextDouble() * 100;
    testTickBarCustom.model.barMax = testRandomBarMax;
    // Confirm barMax updated correctly & custom model has changed from default.
    expect(testTickBarCustom.model.barMax, testRandomBarMax);
    expect(testTickBar.model.barMax, isNot(testTickBarCustom.model.barMax));
    // Change barMax value again to confirm continuous animation updates possible.
    testRandomBarMax = Random().nextDouble() * 100;
    testTickBarCustom.model.barMax = testRandomBarMax;
    // Confirm barMax updated correctly again & custom model has changed from default.
    expect(testTickBarCustom.model.barMax, testRandomBarMax);
    expect(testTickBar.model.barMax, isNot(testTickBarCustom.model.barMax));
  });

  tearDown(() async {
    testTickBar = null;
    testTickBarCustom = null;
    testTextAlign = null;
    testTextStyle = null;
  });
}
