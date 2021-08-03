// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// ignore_for_file: implementation_imports

import 'dart:async';
import 'dart:ui';

import 'package:ermine/src/states/app_state.dart';
import 'package:ermine/src/states/view_state.dart';
import 'package:ermine/src/widgets/app.dart';
import 'package:ermine/src/widgets/app_view.dart';
import 'package:ermine/src/widgets/overlays.dart';
import 'package:ermine_utils/ermine_utils.dart';
import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:fuchsia_logger/logger.dart';
import 'package:intl/intl.dart';
import 'package:mobx/mobx.dart' hide when;
import 'package:mockito/mockito.dart';

void main() async {
  setupLogger(name: 'next_unittests');
  late App app;
  late MockAppState state;

  setUp(() {
    state = MockAppState();
    app = App(state);

    when(state.theme).thenAnswer((_) => AppTheme.darkTheme.asObservable());
    WidgetFactory.mockFactory = (type) => Container(key: ValueKey(type));
  });

  tearDown(() async {
    WidgetFactory.mockFactory = null;
  });

  testWidgets('Test locale change', (tester) async {
    final controller = StreamController<Locale>();
    final stream = controller.stream.asObservable();
    when(state.localeStream).thenAnswer((_) => stream);
    when(state.views).thenAnswer((_) => <ViewState>[].asObservable());
    when(state.overlaysVisible).thenAnswer((_) => false.asObservable());

    await tester.pumpWidget(app);
    // app should be OffStage until locale is pushed.
    expect(find.byType(MaterialApp), findsNothing);

    // Set default locale.
    final defaultLocale = Locale('en', 'US');
    controller.add(defaultLocale);
    await tester.pumpAndSettle();
    expect(find.byType(MaterialApp), findsOneWidget);
    expect(Intl.defaultLocale, defaultLocale.toString());

    // Switch locale to swiss french.
    final swissFrench = Locale('fr', 'CH');
    controller.add(swissFrench);
    await tester.pumpAndSettle();
    expect(find.byType(MaterialApp), findsOneWidget);
    expect(Intl.defaultLocale, swissFrench.toString());
    await controller.close();
  });

  testWidgets('AppView with Overlays is visible', (tester) async {
    final controller = StreamController<Locale>();
    final stream =
        controller.stream.asObservable(initialValue: Locale('en', 'US'));
    when(state.localeStream).thenAnswer((_) => stream);

    // Create one view.
    when(state.views).thenAnswer((_) => [MockViewState()].asObservable());
    // Show overlays.
    when(state.overlaysVisible).thenAnswer((_) => true.asObservable());

    await tester.pumpWidget(app);
    await tester.pumpAndSettle();
    expect(find.byKey(ValueKey(AppView)), findsOneWidget);
    expect(find.byKey(ValueKey(Overlays)), findsOneWidget);
    await controller.close();
  });
}

class MockAppState extends Mock implements AppState {}

class MockViewState extends Mock implements ViewState {}
