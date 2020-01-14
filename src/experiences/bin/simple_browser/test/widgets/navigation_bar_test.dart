// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
import 'dart:async';

import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:fuchsia_logger/logger.dart';
import 'package:mockito/mockito.dart';

// ignore_for_file: implementation_imports
import 'package:simple_browser/src/blocs/tabs_bloc.dart';
import 'package:simple_browser/src/blocs/webpage_bloc.dart';
import 'package:simple_browser/src/models/tabs_action.dart';
import 'package:simple_browser/src/services/simple_browser_navigation_event_listener.dart';
import 'package:simple_browser/src/services/simple_browser_web_service.dart';
import 'package:simple_browser/src/widgets/history_buttons.dart';
import 'package:simple_browser/src/widgets/navigation_bar.dart';
import 'package:simple_browser/src/widgets/navigation_field.dart';

void main() {
  setupLogger(name: 'navigation_bar_test');

  WebPageBloc webPageBloc;
  MockSimpleBrowserWebService mockWebService;
  MockSimpleBrowserNavigationEventListener mockEventListener;

  ValueNotifier backStateNotifier = ValueNotifier<bool>(false);
  ValueNotifier forwardStateNotifier = ValueNotifier<bool>(false);
  ValueNotifier urlNotifier = ValueNotifier<String>('');
  ValueNotifier pageTypeNotifier = ValueNotifier<PageType>(PageType.empty);
  ValueNotifier isLoadedStateNotifier = ValueNotifier<bool>(true);

  group('The bloc is null.', () {
    testWidgets('Should create two empty containers and one + button.',
        (WidgetTester tester) async {
      await _setUpNavigationBar(tester, webPageBloc, () {});

      const reasonSuffix =
          'when the NavigationBar widget has a null webPageBloc.';

      expect(find.byType(Container), findsNWidgets(3),
          reason: 'Expected three containers $reasonSuffix');

      // Sees if there are two empty Containers.
      expect(
          find.byWidgetPredicate(
            (Widget widget) => widget is Container && widget.child == null,
            description: 'Empty containers.',
          ),
          findsNWidgets(2),
          reason: 'Expected two of those containers were empty $reasonSuffix');

      // Sees if there are one + button.
      expect(_findNewTabButton(), findsOneWidget,
          reason:
              'Expected one of those containers was a + button $reasonSuffix');
    });
  });

  group('The bloc is not null.', () {
    setUp(() {
      mockEventListener = MockSimpleBrowserNavigationEventListener();
      when(mockEventListener.backStateNotifier)
          .thenAnswer((_) => backStateNotifier);
      when(mockEventListener.forwardStateNotifier)
          .thenAnswer((_) => forwardStateNotifier);
      when(mockEventListener.urlNotifier).thenAnswer((_) => urlNotifier);
      when(mockEventListener.isLoadedStateNotifier)
          .thenAnswer((_) => isLoadedStateNotifier);

      when(mockEventListener.backState)
          .thenAnswer((_) => backStateNotifier.value);
      when(mockEventListener.forwardState)
          .thenAnswer((_) => forwardStateNotifier.value);
      when(mockEventListener.pageType)
          .thenAnswer((_) => pageTypeNotifier.value);
      when(mockEventListener.isLoadedState)
          .thenAnswer((_) => isLoadedStateNotifier.value);

      mockWebService = MockSimpleBrowserWebService();
      when(mockWebService.navigationEventListener)
          .thenAnswer((_) => mockEventListener);
      webPageBloc = WebPageBloc(
        webService: mockWebService,
      );
    });

    testWidgets(
        'Should create one HistoryButtons, one URL field and one + button.',
        (WidgetTester tester) async {
      await _setUpNavigationBar(tester, webPageBloc, () {});

      const reasonSuffix =
          'when the NavigationBar widget has a non-null webPageBloc.';

      expect(find.byType(HistoryButtons), findsOneWidget,
          reason: 'Expected a HistoryButtons widget $reasonSuffix');
      expect(find.byType(NavigationField), findsOneWidget,
          reason: 'Expected a NavigationField widget $reasonSuffix');
      expect(_findNewTabButton(), findsOneWidget,
          reason: 'Expected a + button $reasonSuffix');
    });

    testWidgets('''Should show a progress bar when the page has not been loaded,
        and should not show it anymore when the page loading is complete.''',
        (WidgetTester tester) async {
      await _setUpNavigationBar(tester, webPageBloc, () {});
      isLoadedStateNotifier.value = false;
      await tester.pump();
      expect(find.byType(LinearProgressIndicator), findsOneWidget,
          reason: 'Expected a progress bar when loading has not finished.');

      isLoadedStateNotifier.value = true;
      await tester.pumpAndSettle();
      expect(find.byType(LinearProgressIndicator), findsNothing,
          reason: 'Expected no progress bars when loading has completed.');
    });

    testWidgets('Should call the newTab callback when the + button is tapped.',
        (WidgetTester tester) async {
      TabsBloc tb = TabsBloc(
        tabFactory: () => MockWebPageBloc(),
        disposeTab: (tab) => tab.dispose(),
      );

      await _setUpNavigationBar(
          tester, webPageBloc, () => tb.request.add(NewTabAction()));

      expect(tb.tabs.length, 0,
          reason:
              'Expected no tabs in the tabsBloc when none has been added to it.');

      final newTabBtn = _findNewTabButton();
      expect(newTabBtn, findsOneWidget,
          reason: 'Expected one + button on the NavigationBar.');
      await tester.tap(newTabBtn);
      await tester.pumpAndSettle();
      expect(tb.tabs.length, 1,
          reason:
              'Expected a tab in the tabsBloc when the + button was tapped.');
    });
  });
}

Future<void> _setUpNavigationBar(
    WidgetTester tester, WebPageBloc bloc, Function callback) async {
  await tester.pumpWidget(
    MaterialApp(
      home: Scaffold(
        body: NavigationBar(
          bloc: bloc,
          newTab: callback,
        ),
      ),
    ),
  );
}

Finder _findNewTabButton() => find.text('+');

class MockSimpleBrowserNavigationEventListener extends Mock
    implements SimpleBrowserNavigationEventListener {}

class MockSimpleBrowserWebService extends Mock
    implements SimpleBrowserWebService {}

class MockWebPageBloc extends Mock implements WebPageBloc {}
