// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:fuchsia_logger/logger.dart';
import 'package:mockito/mockito.dart';

// ignore_for_file: implementation_imports
import 'package:simple_browser/src/blocs/webpage_bloc.dart';
import 'package:simple_browser/src/services/simple_browser_navigation_event_listener.dart';
import 'package:simple_browser/src/services/simple_browser_web_service.dart';
import 'package:simple_browser/src/widgets/navigation_field.dart';

void main() {
  setupLogger(name: 'navigation_field_test');

  SimpleBrowserWebService mockWebService;
  SimpleBrowserNavigationEventListener mockEventListener;
  WebPageBloc webPageBloc;

  setUpAll(() {
    mockWebService = MockSimpleBrowserWebService();
    mockEventListener = MockSimpleBrowserNavigationEventListener();
    webPageBloc = WebPageBloc(webService: mockWebService);
  });

  group('Default textfield (without URL)', () {
    ValueNotifier urlNotifier = ValueNotifier<String>('');

    setUp(() {
      when(mockEventListener.urlNotifier).thenAnswer((_) => urlNotifier);
      when(mockEventListener.url).thenAnswer((_) => urlNotifier.value);
      when(mockWebService.navigationEventListener)
          .thenAnswer((_) => mockEventListener);
    });

    const whenSuffix = 'when created it empty.';
    testWidgets('Should focus on the textfield $whenSuffix',
        (WidgetTester tester) async {
      await _setUpNavigationField(tester, webPageBloc);

      final textField = _findTextField();
      expect(tester.widget<TextField>(textField).autofocus, true,
          reason:
              'Expected the TextField to be focused by default $whenSuffix');
    });

    testWidgets('Should call the callback when a valid url is entered.',
        (WidgetTester tester) async {
      await _setUpNavigationField(tester, webPageBloc);

      String testUrl = 'https://www.google.com';
      final textField = _findTextField();

      // Enters the testUrl to the text field and submit it.
      await tester.enterText(textField, testUrl);
      await tester.testTextInput.receiveAction(TextInputAction.go);
      await tester.pump();

      // Sees if the corresponding callback is called.
      verify(webPageBloc.webService.loadUrl(testUrl)).called(1);
    });
  });

  group('Textfield with a URL', () {
    ValueNotifier urlNotifier = ValueNotifier<String>('');

    setUp(() {
      when(mockEventListener.urlNotifier).thenAnswer((_) => urlNotifier);
      when(mockEventListener.url).thenAnswer((_) => urlNotifier.value);
      when(mockWebService.navigationEventListener)
          .thenAnswer((_) => mockEventListener);
    });

    const whenSuffix = 'when created it with a url.';

    testWidgets('Should not focus on the TextField $whenSuffix',
        (WidgetTester tester) async {
      urlNotifier.value = 'https://www.google.com';

      await _setUpNavigationField(tester, webPageBloc);

      final textField = _findTextField();
      expect(tester.widget<TextField>(textField).autofocus, false,
          reason: 'Expected the textfield not focused by default $whenSuffix');
    });
  });
}

Future<void> _setUpNavigationField(
    WidgetTester tester, WebPageBloc bloc) async {
  await tester.pumpWidget(
    MaterialApp(
      home: Scaffold(
        body: NavigationField(
          bloc: bloc,
        ),
      ),
    ),
  );
}

Finder _findTextField() {
  final textField = find.byType(TextField);
  expect(textField, findsOneWidget,
      reason: 'Expected a TextField on the NavigationField widget.');

  return textField;
}

class MockSimpleBrowserNavigationEventListener extends Mock
    implements SimpleBrowserNavigationEventListener {}

class MockSimpleBrowserWebService extends Mock
    implements SimpleBrowserWebService {}
