// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';

// ignore_for_file: implementation_imports
import 'package:terminal_settings/src/widgets/settings_container.dart';
import 'package:terminal_settings/src/widgets/settings_pane.dart';

void main() {
  testWidgets('empty pane list does not fail', (tester) async {
    final widget = _wrapInMaterialApp(SettingsContainer([]));
    await tester.pumpWidget(widget);
    // nothing here, test success is not crashing
  });

  testWidgets('creates button with title', (tester) async {
    final widget = _wrapInMaterialApp(SettingsContainer([
      _TestSettingsPane(title: 'foo'),
    ]));

    await tester.pumpWidget(widget);

    expect(_findMaterialButtonWithText('foo'), findsOneWidget);
  });

  testWidgets('creates multiple buttons', (tester) async {
    final widget = _wrapInMaterialApp(SettingsContainer([
      _TestSettingsPane(title: 'foo'),
      _TestSettingsPane(title: 'bar'),
    ]));

    await tester.pumpWidget(widget);

    expect(_findMaterialButtonWithText('foo'), findsOneWidget);
    expect(_findMaterialButtonWithText('bar'), findsOneWidget);
  });

  testWidgets('first pane is placed in body', (tester) async {
    final widget = _wrapInMaterialApp(SettingsContainer([
      _TestSettingsPane(title: 'foo', body: 'foo pane'),
      _TestSettingsPane(title: 'bar', body: 'bar pane'),
    ]));

    await tester.pumpWidget(widget);

    expect(find.text('foo pane'), findsOneWidget);
  });

  testWidgets('tapping button changes pane', (tester) async {
    final widget = _wrapInMaterialApp(SettingsContainer([
      _TestSettingsPane(title: 'foo', body: 'foo pane'),
      _TestSettingsPane(title: 'bar', body: 'bar pane'),
    ]));

    await tester.pumpWidget(widget);
    await tester.tap(_findMaterialButtonWithText('bar'));

    await tester.pump();

    expect(find.text('foo pane'), findsNothing);
    expect(find.text('bar pane'), findsOneWidget);
  });
}

Finder _findMaterialButtonWithText(String text) => find.ancestor(
      of: find.text(text),
      matching: find.byWidgetPredicate((w) => w is MaterialButton),
    );

// our widget needs to be in a material app to layout text.
Widget _wrapInMaterialApp(Widget w) => MaterialApp(
      // theme: ThemeData(primaryColor: Colors.yellow),
      home: Scaffold(
        body: w,
      ),
    );

class _TestSettingsPane extends SettingsPane {
  final String body;
  const _TestSettingsPane({String title, this.body}) : super(title: title);

  @override
  Widget build(BuildContext context) {
    return Text(body ?? '');
  }
}
