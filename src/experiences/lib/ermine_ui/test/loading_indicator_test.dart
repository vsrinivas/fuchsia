// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:ermine_ui/ermine_ui.dart';
import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';

void main() {
  Widget _wrap(Widget widget) => MaterialApp(
        home: Scaffold(
          body: widget,
        ),
      );

  testWidgets('The color index should be updated in every 100ms by default.',
      (WidgetTester tester) async {
    const key = Key('indicator');
    const indicator = LoadingIndicator(key: key);
    await tester.pumpWidget(_wrap(indicator));

    final StatefulElement element = tester.element(find.byKey(key));
    // ignore: avoid_as
    final state = element.state as LoadingIndicatorState;
    expect(state.widget, equals(indicator));
    expect(state.firstColorIndex, 0);

    const defaultTime = 100;

    await tester.pump(const Duration(milliseconds: defaultTime + 1));
    expect(state.firstColorIndex, 7);

    await tester.pump(const Duration(milliseconds: defaultTime));
    expect(state.firstColorIndex, 6);

    await tester.pump(const Duration(milliseconds: defaultTime));
    expect(state.firstColorIndex, 5);

    await tester.pump(const Duration(milliseconds: defaultTime));
    expect(state.firstColorIndex, 4);

    await tester.pump(const Duration(milliseconds: defaultTime));
    expect(state.firstColorIndex, 3);

    await tester.pump(const Duration(milliseconds: defaultTime));
    expect(state.firstColorIndex, 2);

    await tester.pump(const Duration(milliseconds: defaultTime));
    expect(state.firstColorIndex, 1);

    await tester.pump(const Duration(milliseconds: defaultTime));
    expect(state.firstColorIndex, 0);
  });

  testWidgets('The color index should be updated in every given time.',
      (WidgetTester tester) async {
    const key = Key('indicator');
    const customTime = 200;
    const indicator = LoadingIndicator(key: key, speedMs: customTime);
    await tester.pumpWidget(_wrap(indicator));

    final StatefulElement element = tester.element(find.byKey(key));
    // ignore: avoid_as
    final state = element.state as LoadingIndicatorState;
    expect(state.widget, equals(indicator));
    expect(state.firstColorIndex, 0);

    await tester.pump(const Duration(milliseconds: customTime + 1));
    expect(state.firstColorIndex, 7);

    await tester.pump(const Duration(milliseconds: customTime));
    expect(state.firstColorIndex, 6);

    await tester.pump(const Duration(milliseconds: customTime));
    expect(state.firstColorIndex, 5);

    await tester.pump(const Duration(milliseconds: customTime));
    expect(state.firstColorIndex, 4);

    await tester.pump(const Duration(milliseconds: customTime));
    expect(state.firstColorIndex, 3);

    await tester.pump(const Duration(milliseconds: customTime));
    expect(state.firstColorIndex, 2);

    await tester.pump(const Duration(milliseconds: customTime));
    expect(state.firstColorIndex, 1);

    await tester.pump(const Duration(milliseconds: customTime));
    expect(state.firstColorIndex, 0);
  });

  testWidgets('The indicator should display the description when one is given.',
      (WidgetTester tester) async {
    const description = 'Ermine loading indicator';
    const indicator = LoadingIndicator(description: description);
    await tester.pumpWidget(_wrap(indicator));

    expect(find.text(description), findsOneWidget);
  });
}
