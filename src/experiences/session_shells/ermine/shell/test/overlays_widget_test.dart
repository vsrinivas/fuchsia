// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// ignore_for_file: implementation_imports

import 'package:ermine/src/states/app_state.dart';
import 'package:ermine/src/widgets/app_bar.dart';
import 'package:ermine/src/widgets/app_switcher.dart';
import 'package:ermine/src/widgets/overlays.dart';
import 'package:ermine/src/widgets/scrim.dart';
import 'package:ermine/src/widgets/side_bar.dart';
import 'package:ermine_utils/ermine_utils.dart';
import 'package:flutter/material.dart' hide AppBar;
import 'package:flutter_test/flutter_test.dart';
import 'package:mobx/mobx.dart' hide when;
import 'package:mockito/mockito.dart';

void main() async {
  late Widget overlays;
  late MockAppState app;
  final appBarVisible = false.asObservable();
  final sideBarVisible = false.asObservable();
  final switcherVisible = false.asObservable();
  final userFeedbackVisible = false.asObservable();

  setUp(() {
    app = MockAppState();
    overlays = Directionality(
      child: Overlays(app),
      textDirection: TextDirection.ltr,
    );

    when(app.appBarVisible).thenAnswer((_) => appBarVisible.value);
    when(app.sideBarVisible).thenAnswer((_) => sideBarVisible.value);
    when(app.switcherVisible).thenAnswer((_) => switcherVisible.value);
    when(app.userFeedbackVisible).thenAnswer((_) => userFeedbackVisible.value);

    WidgetFactory.mockFactory = (type) => Container(key: ValueKey(type));
  });

  tearDown(() async {
    WidgetFactory.mockFactory = null;
  });

  testWidgets('Scrim is always present', (tester) async {
    await tester.pumpWidget(overlays);
    expect(find.byKey(ValueKey(Scrim)), findsOneWidget);
  });

  testWidgets('Appbar is visible', (tester) async {
    await tester.pumpWidget(overlays);
    expect(find.byKey(ValueKey(AppBar)), findsNothing);

    runInAction(() => appBarVisible.value = true);
    await tester.pumpAndSettle();
    expect(find.byKey(ValueKey(AppBar)), findsOneWidget);
  });

  testWidgets('Sidebar is visible', (tester) async {
    await tester.pumpWidget(overlays);
    expect(find.byKey(ValueKey(SideBar)), findsNothing);

    runInAction(() => sideBarVisible.value = true);
    await tester.pumpAndSettle();
    expect(find.byKey(ValueKey(SideBar)), findsOneWidget);
  });

  testWidgets('Switcher is visible', (tester) async {
    await tester.pumpWidget(overlays);
    expect(find.byKey(ValueKey(AppSwitcher)), findsNothing);

    runInAction(() => switcherVisible.value = true);
    await tester.pumpAndSettle();
    expect(find.byKey(ValueKey(AppSwitcher)), findsOneWidget);
  });
}

class MockAppState extends Mock implements AppState {}
