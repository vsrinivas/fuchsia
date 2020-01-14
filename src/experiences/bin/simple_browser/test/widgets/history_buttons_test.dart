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
import 'package:simple_browser/src/widgets/history_buttons.dart';

enum ButtonType {
  back,
  forward,
  refresh,
}

void main() {
  setupLogger(name: 'history_buttons_test');

  WebPageBloc webPageBloc;
  MockSimpleBrowserWebService mockWebService;
  MockSimpleBrowserNavigationEventListener mockEventListener;

  ValueNotifier backStateNotifier = ValueNotifier<bool>(false);
  ValueNotifier forwardStateNotifier = ValueNotifier<bool>(false);
  ValueNotifier urlNotifier = ValueNotifier<String>('');
  ValueNotifier pageTypeNotifier = ValueNotifier<PageType>(PageType.empty);

  setUpAll(() {
    mockEventListener = MockSimpleBrowserNavigationEventListener();
    when(mockEventListener.backStateNotifier)
        .thenAnswer((_) => backStateNotifier);
    when(mockEventListener.forwardStateNotifier)
        .thenAnswer((_) => forwardStateNotifier);
    when(mockEventListener.urlNotifier).thenAnswer((_) => urlNotifier);

    when(mockEventListener.backState)
        .thenAnswer((_) => backStateNotifier.value);
    when(mockEventListener.forwardState)
        .thenAnswer((_) => forwardStateNotifier.value);
    when(mockEventListener.pageType).thenAnswer((_) => pageTypeNotifier.value);

    mockWebService = MockSimpleBrowserWebService();
    when(mockWebService.navigationEventListener)
        .thenAnswer((_) => mockEventListener);
    webPageBloc = WebPageBloc(
      webService: mockWebService,
    );
  });

  testWidgets('There should be 3 text widgets: BCK, FWD, and RFRSH.',
      (WidgetTester tester) async {
    await _setUpHistoryButtons(tester, webPageBloc);

    // Sees if there are a ‘BCK’, a 'FWD' and a 'RFRSH' texts.
    expect(find.text('BCK'), findsOneWidget);
    expect(find.text('FWD'), findsOneWidget);
    expect(find.text('RFRSH'), findsOneWidget);
  });

  group('Buttons are all disabled', () {
    testWidgets('A disalbed button should not work when tapped.',
        (WidgetTester tester) async {
      await _setUpHistoryButtons(tester, webPageBloc);

      final historyButtons = _findHistoryButtons();

      // Taps the back button and sees whether it works or not.
      await _tapHistoryButton(tester, historyButtons, ButtonType.back);
      _verifyAllNeverWork(webPageBloc);

      // Taps the forward button and sees whether it works or not.
      await _tapHistoryButton(tester, historyButtons, ButtonType.forward);
      _verifyAllNeverWork(webPageBloc);

      // Taps the refresh button and sees whether it works or not.
      await _tapHistoryButton(tester, historyButtons, ButtonType.refresh);
      _verifyAllNeverWork(webPageBloc);
    });
  });

  group('Buttons are all enabled', () {
    String testUrl;

    // Set-ups for enabling the history buttons.
    setUp(() {
      testUrl = 'https://www.google.com';
      backStateNotifier.value = true;
      forwardStateNotifier.value = true;
      urlNotifier.value = testUrl;
      pageTypeNotifier.value = PageType.normal;
    });

    testWidgets('An enabled button should work when tapped.',
        (WidgetTester tester) async {
      await _setUpHistoryButtons(tester, webPageBloc);

      final historyButtons = _findHistoryButtons();

      // Taps the back button and sees whether it works or not.
      await _tapHistoryButton(tester, historyButtons, ButtonType.back);
      verify(webPageBloc.webService.goBack());
      verifyNever(webPageBloc.webService.goForward());
      verifyNever(webPageBloc.webService.refresh());

      // Taps the forward button and sees whether it works or not.
      await _tapHistoryButton(tester, historyButtons, ButtonType.forward);
      verifyNever(webPageBloc.webService.goBack());
      verify(webPageBloc.webService.goForward());
      verifyNever(webPageBloc.webService.refresh());

      // Taps the refresh button and sees whether it works or not.
      await _tapHistoryButton(tester, historyButtons, ButtonType.refresh);
      verifyNever(webPageBloc.webService.goBack());
      verifyNever(webPageBloc.webService.goForward());
      verify(webPageBloc.webService.refresh());
    });
  });
}

Future<void> _setUpHistoryButtons(
  WidgetTester tester,
  WebPageBloc bloc,
) async {
  await tester.pumpWidget(
    MaterialApp(
      home: Scaffold(
        body: HistoryButtons(
          bloc: bloc,
        ),
      ),
    ),
  );
  await tester.pumpAndSettle();

  expect(_findHistoryButtons(), findsNWidgets(3),
      reason: 'Expected 3 history buttons on the HistoryButtons widget.');
}

Finder _findHistoryButtons() => find.byType(GestureDetector);

Future<void> _tapHistoryButton(
    WidgetTester tester, Finder buttons, ButtonType target) async {
  int index = target.index;

  await tester.tap(buttons.at(index));
  await tester.pumpAndSettle();
}

void _verifyAllNeverWork(WebPageBloc bloc) {
  verifyNever(bloc.webService.goBack());
  verifyNever(bloc.webService.goForward());
  verifyNever(bloc.webService.refresh());
}

class MockSimpleBrowserNavigationEventListener extends Mock
    implements SimpleBrowserNavigationEventListener {}

class MockSimpleBrowserWebService extends Mock
    implements SimpleBrowserWebService {}
