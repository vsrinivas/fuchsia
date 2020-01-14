// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:fuchsia_logger/logger.dart';

// ignore_for_file: implementation_imports
import 'package:simple_browser/src/widgets/error_page.dart';

void main() {
  setupLogger(name: 'error_page_test');

  double bodyWidth;
  double bodyHeight;

  setUpAll(() {
    bodyWidth = 800.0;
    bodyHeight = 600.0;
  });

  testWidgets('There should be 5 text widgets: E,R,R,O,R.',
      (WidgetTester tester) async {
    await _setUpErrorPage(tester, bodyWidth, bodyHeight);

    // Sees if there are one ‘E’, one ‘O’ and three ‘R’ texts.
    expect(find.text('E'), findsOneWidget,
        reason: 'Expected an E on the error page.');
    expect(find.text('O'), findsOneWidget,
        reason: 'Expected an O on the error page.');
    expect(find.text('R'), findsNWidgets(3),
        reason: 'Expected three Rs on the error page.');
  });

  testWidgets('There should be 5 Positioned widgets in the intended order.',
      (WidgetTester tester) async {
    await _setUpErrorPage(tester, bodyWidth, bodyHeight);

    // Sees if there are 5 Positioned widgets
    expect(find.byType(Positioned), findsNWidgets(5),
        reason: 'Expected 5 Positioned widgets on the error page.');

    // Sees if all those Positioned widgets are positioned on the intended locations.

    // Verifies the left offsets of the widgets.
    final e = find.text('E');
    final r = find.text('R');
    final o = find.text('O');

    // Sees if each character is displayed in the correct order.
    _expectAToBeFollowedByB(tester, e, r.at(0));
    _expectAToBeFollowedByB(tester, r.at(0), r.at(1));
    _expectAToBeFollowedByB(tester, r.at(1), o);
    _expectAToBeFollowedByB(tester, o, r.at(2));
  });
}

Future<void> _setUpErrorPage(
    WidgetTester tester, double width, double height) async {
  await tester.pumpWidget(MaterialApp(
    home: Scaffold(
      body: Container(
        width: width,
        height: height,
        child: ErrorPage(),
      ),
    ),
  ));
}

void _expectAToBeFollowedByB(WidgetTester tester, Finder a, Finder b) {
  expect(tester.getTopLeft(a).dx < tester.getTopLeft(b).dx, true,
      reason: 'Expected $a to be followed by $b when an error page created.');
}
