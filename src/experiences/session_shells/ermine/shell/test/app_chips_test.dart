// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// ignore_for_file: implementation_imports, cast_from_null_always_fails

import 'package:ermine/src/states/app_state.dart';
import 'package:ermine/src/states/view_state.dart';
import 'package:ermine/src/widgets/app_chips.dart';
import 'package:ermine_utils/ermine_utils.dart';
import 'package:flutter/material.dart' hide AppBar;
import 'package:flutter_test/flutter_test.dart';
import 'package:mobx/mobx.dart' hide when;
import 'package:mockito/mockito.dart';

void main() async {
  late Widget appChips;
  late MockAppState app;
  final fooView = MockViewState();
  final barView = MockViewState();
  final views = [fooView, barView].asObservable();

  setUp(() {
    app = MockAppState();
    appChips = MaterialApp(home: Material(child: AppChips(app)));

    when(fooView.title).thenReturn('Foo');
    when(barView.title).thenReturn('Bar');
    when(app.views).thenAnswer((_) => views);
    when(app.appLaunchEntries).thenAnswer((_) => [
          {'title': 'Foo', 'url': 'about:blank'},
          {'title': 'Bar', 'url': 'about:blank'},
        ].asObservable());

    WidgetFactory.mockFactory = (type) => Container(key: ValueKey(type));
  });

  tearDown(() async {
    WidgetFactory.mockFactory = null;
  });

  testWidgets('Switches to view on tap', (tester) async {
    await tester.pumpWidget(appChips);
    // Two view entries should exist.
    expect(find.text('Foo'), findsOneWidget);
    expect(find.text('Bar'), findsOneWidget);

    // Tap 'Foo' to switch to it.
    await tester.tap(find.text('Foo'));
    verify(app.switchView(any as ViewState));
  });

  testWidgets('Closes the view on tap', (tester) async {
    await tester.pumpWidget(appChips);
    await tester.tap(find.byKey(ValueKey('appChipClose-0')));
    verify(fooView.close());

    // Remove the view and verify the UI is updated.
    runInAction(() => views.remove(fooView));
    await tester.pumpAndSettle();
    expect(find.text('Foo'), findsNothing);
  });
}

class MockAppState extends Mock implements AppState {}

class MockViewState extends Mock implements ViewState {}
