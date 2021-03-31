// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// ignore_for_file: implementation_imports
import 'package:flutter_test/flutter_test.dart';
import 'package:fuchsia_inspect/inspect.dart' as inspect;
import 'package:mockito/mockito.dart';
import 'package:torus15/src/ui/torus_grid.dart';

class MockNode extends Mock implements inspect.Node {}

void main() {
  testWidgets('4 x 4 Torus Puzzle has numbered tiles',
      (WidgetTester tester) async {
    await tester.pumpWidget(TorusGrid(
      cols: 4,
      rows: 4,
    ));

    // confirm presence of all tiles
    for (int i = 0; i < 16; i++) {
      final tileFinder = find.text('$i');
      expect(tileFinder, findsOneWidget);
    }
  });

  testWidgets('Swipe right', (WidgetTester tester) async {
    await tester.pumpWidget(TorusGrid(
      cols: 4,
      rows: 4,
    ));

    const flingSpeed = 20.0;

    // swipe right on 0 tile
    await tester.fling(find.text('0'), Offset(1, 0), flingSpeed);
    await tester.pump();

    // confirm presence of all tiles
    for (int i = 0; i < 16; i++) {
      final tileFinder = find.text('$i');
      expect(tileFinder, findsOneWidget);
    }
  });
}
