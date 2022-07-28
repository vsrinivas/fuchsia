// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// ignore_for_file: implementation_imports

import 'package:ermine/src/states/app_state.dart';
import 'package:ermine/src/states/settings_state.dart';
import 'package:ermine/src/widgets/quick_settings.dart';
import 'package:ermine_utils/ermine_utils.dart';
import 'package:flutter/material.dart' hide AppBar;
import 'package:flutter_test/flutter_test.dart';
import 'package:mockito/mockito.dart';

void main() async {
  late Widget quickSettings;
  late MockAppState app;
  late MockSettingsState settings;

  final hasDarkTheme = true.asObservable();
  final isUserFeedbackEnabled = false.asObservable();
  final shortcutsPageVisible = false.asObservable();
  final timezonesPageVisible = false.asObservable();
  final aboutPageVisible = false.asObservable();
  final channelPageVisible = false.asObservable();
  final wifiPageVisible = false.asObservable();
  final keyboardPageVisible = false.asObservable();
  final dataSharingConsentPageVisible = false.asObservable();
  final dateTime = '12:00 AM'.asObservable();
  final scale = 1.0.asObservable();
  final currentNetwork = 'WiFi'.asObservable();
  final brightnessLevel = 1.0.asObservable();
  final brightnessAuto = false.asObservable();
  final volumeLevel = 1.0.asObservable();
  final volumeMuted = false.asObservable();
  final currentKeymap = 'usQwerty'.asObservable();

  setUp(() {
    app = MockAppState();
    settings = MockSettingsState();
    quickSettings = MaterialApp(
      home: Material(child: QuickSettings(app)),
      // textDirection: TextDirection.ltr,
    );

    when(app.settingsState).thenReturn(settings);
    when(app.hasDarkTheme).thenAnswer((_) => hasDarkTheme.value);
    when(app.isUserFeedbackEnabled)
        .thenAnswer((_) => isUserFeedbackEnabled.value);
    when(app.scale).thenAnswer((_) => scale.value);
    when(settings.dateTime).thenAnswer((_) => dateTime.value);
    when(settings.selectedTimezone).thenReturn('America/Los_Angeles');
    when(settings.currentChannel).thenReturn('testing');
    when(settings.currentNetwork).thenAnswer((_) => currentNetwork.value);
    when(settings.brightnessLevel).thenAnswer((_) => brightnessLevel.value);
    when(settings.brightnessAuto).thenAnswer((_) => brightnessAuto.value);
    when(settings.volumeLevel).thenAnswer((_) => volumeLevel.value);
    when(settings.volumeMuted).thenAnswer((_) => volumeMuted.value);
    when(settings.currentKeymap).thenAnswer((_) => currentKeymap.value);
    when(settings.shortcutsPageVisible)
        .thenAnswer((_) => shortcutsPageVisible.value);
    when(settings.timezonesPageVisible)
        .thenAnswer((_) => timezonesPageVisible.value);
    when(settings.aboutPageVisible).thenAnswer((_) => aboutPageVisible.value);
    when(settings.channelPageVisible)
        .thenAnswer((_) => channelPageVisible.value);
    when(settings.wifiPageVisible).thenAnswer((_) => wifiPageVisible.value);
    when(settings.keyboardPageVisible)
        .thenAnswer((_) => keyboardPageVisible.value);
    when(settings.dataSharingConsentPageVisible)
        .thenAnswer((_) => dataSharingConsentPageVisible.value);

    WidgetFactory.mockFactory = (type) => Container(key: ValueKey(type));
  });

  tearDown(() async {
    WidgetFactory.mockFactory = null;
  });

  testWidgets('Dark mode can be toggled', (tester) async {
    await tester.pumpWidget(quickSettings);
    await tester.tap(find.byKey(ValueKey('darkMode')));
    await tester.pumpAndSettle();

    verify(app.setTheme(darkTheme: false));
  });
}

class MockAppState extends Mock implements AppState {}

class MockSettingsState extends Mock implements SettingsState {}
