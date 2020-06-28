// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:async';

import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:fuchsia_logger/logger.dart';
import 'package:intl/intl.dart';
import 'package:mockito/mockito.dart';

// ignore_for_file: implementation_imports
import 'package:ermine/src/models/app_model.dart';
import 'package:ermine/src/widgets/app.dart';

void main() async {
  setupLogger(name: 'ermine_unittests');

  TestApp app;
  MockAppModel model;
  StreamController<Locale> controller;

  setUp(() {
    model = MockAppModel();
    app = TestApp(model);

    controller = StreamController<Locale>();
    when(model.localeStream).thenAnswer((_) => controller.stream);
  });

  tearDown(() async {
    await controller.close();
  });

  testWidgets('Test locale change', (tester) async {
    when(model.overviewVisibility).thenReturn(ValueNotifier<bool>(true));

    await tester.pumpWidget(app);
    // app should be OffStage until locale is pushed.
    expect(find.byType(MaterialApp), findsNothing);

    // Set default locale.
    final defaultLocale = Locale('en', 'US');
    controller.add(defaultLocale);
    await tester.pumpAndSettle();
    expect(Intl.defaultLocale, defaultLocale.toString());

    // Switch locale to swiss french.
    final swissFrench = Locale('fr', 'CH');
    controller.add(swissFrench);
    await tester.pumpAndSettle();
    expect(Intl.defaultLocale, swissFrench.toString());
  });

  testWidgets('Toggle between overview and home containers', (tester) async {
    // Set locale to render the app.
    controller.add(Locale('en', 'US'));

    // Make Overview visible.
    final overviewNotifier = ValueNotifier<bool>(true);
    when(model.overviewVisibility).thenReturn(overviewNotifier);

    await tester.pumpWidget(app);
    await tester.pumpAndSettle();
    await tester.pumpAndSettle();

    expect(find.byWidget(app.recents), findsOneWidget);
    expect(find.byWidget(app.overview), findsOneWidget);
    expect(find.byWidget(app.home), findsNothing);

    // Home should be visible.
    overviewNotifier.value = false;
    await tester.pumpAndSettle();

    expect(find.byWidget(app.recents), findsOneWidget);
    expect(find.byWidget(app.overview), findsNothing);
    expect(find.byWidget(app.home), findsOneWidget);
  });
}

class TestApp extends App {
  final Widget overview = Container();
  final Widget home = Container();
  final Widget recents = Container();

  TestApp(AppModel model) : super(model: model);

  @override
  Widget buildRecents(AppModel model) => recents;

  @override
  Widget buildOverview(AppModel model) => overview;

  @override
  Widget buildHome(AppModel model) => home;
}

class MockAppModel extends Mock implements AppModel {}
