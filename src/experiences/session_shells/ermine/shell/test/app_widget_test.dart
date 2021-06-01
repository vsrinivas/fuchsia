// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:async';

// ignore_for_file: implementation_imports
import 'package:ermine/src/models/app_model.dart';
import 'package:ermine/src/widgets/app.dart';
import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:fuchsia_logger/logger.dart';
import 'package:intl/intl.dart';
import 'package:mockito/mockito.dart';

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
    when(model.oobeVisibility).thenReturn(ValueNotifier<bool>(false));

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

    // OOBE is turned off.
    when(model.oobeVisibility).thenReturn(ValueNotifier<bool>(false));

    await tester.pumpWidget(app);
    await tester.pumpUntilVisible(app.overview);

    expect(find.byWidget(app.recents), findsOneWidget);
    expect(find.byWidget(app.overview), findsOneWidget);
    expect(find.byWidget(app.home), findsNothing);
    expect(find.byWidget(app.alert), findsOneWidget);
    expect(find.byWidget(app.oobe), findsNothing);

    // Home should be visible.
    overviewNotifier.value = false;
    await tester.pumpUntilVisible(app.home);

    expect(find.byWidget(app.recents), findsOneWidget);
    expect(find.byWidget(app.overview), findsNothing);
    expect(find.byWidget(app.home), findsOneWidget);
    expect(find.byWidget(app.alert), findsOneWidget);
    expect(find.byWidget(app.oobe), findsNothing);
  });
}

class TestApp extends App {
  final Widget overview = Container();
  final Widget home = Container();
  final Widget recents = Container();
  final Widget alert = Container();
  final Widget oobe = Container();

  TestApp(AppModel model) : super(model: model);

  @override
  Widget buildRecents(AppModel model) => recents;

  @override
  Widget buildOverview(AppModel model) => overview;

  @override
  Widget buildHome(AppModel model) => home;

  @override
  Widget buildAlert(AppModel model) => alert;

  @override
  Widget buildOobe(AppModel model) => oobe;
}

class MockAppModel extends Mock implements AppModel {}

extension _WidgetVisibility on WidgetTester {
  /// Calls [pumpAndSettle] until widget finder finds a widget or the default
  /// timeout of the surrounding test is exceeded.
  Future<void> pumpUntilVisible(Widget widget) async {
    var matchState = {};
    while (findsNothing.matches(find.byWidget(widget), matchState)) {
      await pumpAndSettle();
    }
  }
}
