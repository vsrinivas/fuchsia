// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// ignore_for_file: implementation_imports

import 'package:ermine/src/states/app_state.dart';
import 'package:ermine/src/widgets/app_launcher.dart';
import 'package:ermine/src/widgets/quick_settings.dart';
import 'package:ermine/src/widgets/side_bar.dart';
import 'package:ermine/src/widgets/status.dart';
import 'package:ermine_utils/ermine_utils.dart';
import 'package:flutter/material.dart' hide AppBar;
import 'package:flutter_test/flutter_test.dart';
import 'package:mockito/mockito.dart';

void main() async {
  late Widget sideBar;
  late MockAppState app;

  setUp(() {
    app = MockAppState();
    sideBar = Directionality(
      child: SideBar(app),
      textDirection: TextDirection.ltr,
    );

    WidgetFactory.mockFactory = (type) => Container(key: ValueKey(type));
  });

  tearDown(() async {
    WidgetFactory.mockFactory = null;
  });

  testWidgets('Contains AppLauncher, Status and QuickSettings', (tester) async {
    await tester.pumpWidget(sideBar);
    expect(find.byKey(ValueKey(AppLauncher)), findsOneWidget);
    expect(find.byKey(ValueKey(Status)), findsOneWidget);
    expect(find.byKey(ValueKey(QuickSettings)), findsOneWidget);
  });
}

class MockAppState extends Mock implements AppState {}
