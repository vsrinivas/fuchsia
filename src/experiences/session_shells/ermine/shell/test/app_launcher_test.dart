// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// ignore_for_file: implementation_imports

import 'package:ermine/src/states/app_state.dart';
import 'package:ermine/src/states/view_state.dart';
import 'package:ermine/src/widgets/app_launcher.dart';
import 'package:ermine_utils/ermine_utils.dart';
import 'package:flutter/material.dart' hide AppBar;
import 'package:flutter_test/flutter_test.dart';
import 'package:mockito/mockito.dart';

void main() async {
  late Widget appLauncher;
  late MockAppState app;

  setUp(() {
    app = MockAppState();
    appLauncher = MaterialApp(home: Material(child: AppLauncher(app)));

    when(app.errors)
        .thenAnswer((_) => <String, List<String>>{}.asObservable().value);
    when(app.views).thenAnswer((_) => []);
    when(app.appLaunchEntries).thenAnswer((_) => [
          {'title': 'Foo', 'url': 'about:blank'},
          {'title': 'Bar', 'url': 'about:blank'},
          {'title': 'Null'},
        ].asObservable().value);

    WidgetFactory.mockFactory = (type) => Container(key: ValueKey(type));
  });

  tearDown(() async {
    WidgetFactory.mockFactory = null;
  });

  testWidgets('Launches apps from the launcher', (tester) async {
    await tester.pumpWidget(appLauncher);
    // Two launch entries should exist.
    expect(find.text('Foo'), findsOneWidget);
    expect(find.text('Bar'), findsOneWidget);

    // Tap 'Foo' to launch it.
    await tester.tap(find.text('Foo'));
    verify(app.launch('Foo', 'about:blank'));
  });

  testWidgets('Apps with missing url is disabled', (tester) async {
    await tester.pumpWidget(appLauncher);
    // Null entry should exist in disabled state.
    expect(find.text('Null'), findsOneWidget);
    expect(
        tester.widget<ListTile>(find.byKey(ValueKey('launchItem-2'))).enabled,
        isFalse);
  });

  testWidgets('Does not launch when a view is loading', (tester) async {
    await tester.pumpWidget(appLauncher);

    final view = MockViewState();
    when(view.title).thenReturn('Foo');
    when(view.loading).thenReturn(false);
    when(app.views).thenAnswer((_) => [view]);

    // Tap 'Foo' to launch it and it should fail.
    await tester.tap(find.text('Foo'));
    verifyNever(app.launch('Foo', 'about:blank'));
  });
}

class MockAppState extends Mock implements AppState {}

class MockViewState extends Mock implements ViewState {}
