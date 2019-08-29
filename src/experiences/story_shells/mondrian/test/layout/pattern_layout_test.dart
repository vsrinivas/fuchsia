// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:flutter/widgets.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:fuchsia_logger/logger.dart';
import 'package:mockito/mockito.dart';
import 'package:mondrian/models/layout_model.dart';
import 'package:mondrian/layout/pattern_layout.dart' as pattern_layout;
import 'package:mondrian/models/surface/positioned_surface.dart';
import 'package:mondrian/models/surface/surface.dart';
import '../layout_test_utils.dart' as test_util;

class MockSurface extends Mock implements Surface {}

void main() {
  // pattern layout logs warnings
  setupLogger(name: 'mondrain_story_shell_tests');
  Surface firstSurface = MockSurface();
  LayoutModel layoutModel = LayoutModel();
  test('Ticker pattern with 2 surfaces', () {
    Surface patternSurface = MockSurface();
    when(patternSurface.compositionPattern).thenReturn('ticker');
    List<Surface> surfaces = [
      firstSurface,
      patternSurface,
    ];
    List<PositionedSurface> positionedSurfaces = pattern_layout.layoutSurfaces(
        null /* BuildContext */, surfaces, layoutModel);
    expect(positionedSurfaces.length, 2);

    test_util.assertSurfaceProperties(positionedSurfaces[0],
        height: 0.85, width: 1.0, topLeft: Offset(0.0, 0.0));

    test_util.assertSurfaceProperties(positionedSurfaces[1],
        height: 0.15, width: 1.0, topLeft: Offset(0.0, 0.85));
  });

  test('Ticker pattern with 2 surfaces and empty composition pattern', () {
    when(firstSurface.compositionPattern).thenReturn('');
    Surface patternSurface = MockSurface();
    when(patternSurface.compositionPattern).thenReturn('ticker');
    List<Surface> surfaces = [
      firstSurface,
      patternSurface,
    ];
    List<PositionedSurface> positionedSurfaces = pattern_layout.layoutSurfaces(
        null /* BuildContext */, surfaces, layoutModel);
    expect(positionedSurfaces.length, 2);

    test_util.assertSurfaceProperties(positionedSurfaces[0],
        height: 0.85, width: 1.0, topLeft: Offset(0.0, 0.0));

    test_util.assertSurfaceProperties(positionedSurfaces[1],
        height: 0.15, width: 1.0, topLeft: Offset(0.0, 0.85));
  });

  test('Multiple tickers', () {
    Surface tickerSurface = MockSurface();
    when(tickerSurface.compositionPattern).thenReturn('ticker');
    Surface tickerSurface2 = MockSurface();
    when(tickerSurface2.compositionPattern).thenReturn('ticker');
    Surface tickerSurface3 = MockSurface();
    when(tickerSurface3.compositionPattern).thenReturn('ticker');
    List<Surface> surfaces = [
      firstSurface,
      tickerSurface,
      tickerSurface2,
      tickerSurface3,
    ];
    List<PositionedSurface> positionedSurfaces = pattern_layout.layoutSurfaces(
        null /* BuildContext */, surfaces, layoutModel);
    expect(positionedSurfaces.length, 2);

    test_util.assertSurfaceProperties(positionedSurfaces[0],
        height: 0.85, width: 1.0, topLeft: Offset(0.0, 0.0));

    test_util.assertSurfaceProperties(positionedSurfaces[1],
        height: 0.15, width: 1.0, topLeft: Offset(0.0, 0.85));
    expect(positionedSurfaces[1].surface, tickerSurface3);
  });

  test('Comments-right pattern with 2 surfaces', () {
    Surface patternSurface = MockSurface();
    when(patternSurface.compositionPattern).thenReturn('comments-right');
    List<Surface> surfaces = [
      firstSurface,
      patternSurface,
    ];
    List<PositionedSurface> positionedSurfaces = pattern_layout.layoutSurfaces(
        null /* BuildContext */, surfaces, layoutModel);
    expect(positionedSurfaces.length, 2);

    test_util.assertSurfaceProperties(positionedSurfaces[0],
        height: 1.0, width: 0.7, topLeft: Offset(0.0, 0.0));

    test_util.assertSurfaceProperties(positionedSurfaces[1],
        height: 1.0, width: 0.3, topLeft: Offset(0.7, 0.0));
  });

  test('Multiple comments-right', () {
    Surface commentsSurface = MockSurface();
    when(commentsSurface.compositionPattern).thenReturn('comments-right');
    Surface commentsSurface2 = MockSurface();
    when(commentsSurface2.compositionPattern).thenReturn('comments-right');
    Surface commentsSurface3 = MockSurface();
    when(commentsSurface3.compositionPattern).thenReturn('comments-right');
    List<Surface> surfaces = [
      firstSurface,
      commentsSurface,
      commentsSurface2,
      commentsSurface3,
    ];
    List<PositionedSurface> positionedSurfaces = pattern_layout.layoutSurfaces(
        null /* BuildContext */, surfaces, layoutModel);
    expect(positionedSurfaces.length, 2);

    test_util.assertSurfaceProperties(positionedSurfaces[0],
        height: 1.0, width: 0.7, topLeft: Offset(0.0, 0.0));

    test_util.assertSurfaceProperties(positionedSurfaces[1],
        height: 1.0, width: 0.3, topLeft: Offset(0.7, 0.0));
    expect(positionedSurfaces[1].surface, commentsSurface3);
  });

  test('Undefined pattern', () {
    Surface patternSurface = MockSurface();
    when(patternSurface.compositionPattern).thenReturn('undefined');
    List<Surface> surfaces = [
      firstSurface,
      patternSurface,
    ];
    List<PositionedSurface> positionedSurfaces = pattern_layout.layoutSurfaces(
        null /* BuildContext */, surfaces, layoutModel);
    expect(positionedSurfaces.length, 1);

    test_util.assertSurfaceProperties(positionedSurfaces.first,
        height: 1.0, width: 1.0, topLeft: Offset(0.0, 0.0));
  });

  test('Comments and ticker', () {
    Surface commentsSurface = MockSurface();
    when(commentsSurface.compositionPattern).thenReturn('comments-right');
    Surface tickerSurface = MockSurface();
    when(tickerSurface.compositionPattern).thenReturn('ticker');
    List<Surface> surfaces = [
      firstSurface,
      commentsSurface,
      tickerSurface,
    ];
    List<PositionedSurface> positionedSurfaces = pattern_layout.layoutSurfaces(
        null /* BuildContext */, surfaces, layoutModel);
    expect(positionedSurfaces.length, 3);

    test_util.assertSurfaceProperties(positionedSurfaces[0],
        height: 0.85, width: 0.7, topLeft: Offset(0.0, 0.0));

    test_util.assertSurfaceProperties(positionedSurfaces[1],
        height: 1.0, width: 0.3, topLeft: Offset(0.7, 0.0));

    test_util.assertSurfaceProperties(positionedSurfaces[2],
        height: 0.15, width: 0.7, topLeft: Offset(0.0, 0.85));
  });

  test('Ticker and comments', () {
    Surface commentsSurface = MockSurface();
    when(commentsSurface.compositionPattern).thenReturn('comments-right');
    Surface tickerSurface = MockSurface();
    when(tickerSurface.compositionPattern).thenReturn('ticker');
    List<Surface> surfaces = [
      firstSurface,
      tickerSurface,
      commentsSurface,
    ];
    List<PositionedSurface> positionedSurfaces = pattern_layout.layoutSurfaces(
        null /* BuildContext */, surfaces, layoutModel);
    expect(positionedSurfaces.length, 3);

    test_util.assertSurfaceProperties(positionedSurfaces[0],
        height: 0.85, width: 0.7, topLeft: Offset(0.0, 0.0));

    test_util.assertSurfaceProperties(positionedSurfaces[1],
        height: 1.0, width: 0.3, topLeft: Offset(0.7, 0.0));

    test_util.assertSurfaceProperties(positionedSurfaces[2],
        height: 0.15, width: 0.7, topLeft: Offset(0.0, 0.85));
  });

  test('Multiple ticker and comments', () {
    Surface commentsSurface = MockSurface();
    when(commentsSurface.compositionPattern).thenReturn('comments-right');
    Surface commentsSurface2 = MockSurface();
    when(commentsSurface2.compositionPattern).thenReturn('comments-right');
    Surface tickerSurface = MockSurface();
    when(tickerSurface.compositionPattern).thenReturn('ticker');
    Surface tickerSurface2 = MockSurface();
    when(tickerSurface2.compositionPattern).thenReturn('ticker');
    List<Surface> surfaces = [
      firstSurface,
      tickerSurface,
      commentsSurface,
      tickerSurface2,
      commentsSurface2,
    ];
    List<PositionedSurface> positionedSurfaces = pattern_layout.layoutSurfaces(
        null /* BuildContext */, surfaces, layoutModel);
    expect(positionedSurfaces.length, 3);

    test_util.assertSurfaceProperties(positionedSurfaces[0],
        height: 0.85, width: 0.7, topLeft: Offset(0.0, 0.0));

    test_util.assertSurfaceProperties(positionedSurfaces[1],
        height: 1.0, width: 0.3, topLeft: Offset(0.7, 0.0));
    expect(positionedSurfaces[1].surface, commentsSurface2);

    test_util.assertSurfaceProperties(positionedSurfaces[2],
        height: 0.15, width: 0.7, topLeft: Offset(0.0, 0.85));
    expect(positionedSurfaces[2].surface, tickerSurface2);
  });
}
