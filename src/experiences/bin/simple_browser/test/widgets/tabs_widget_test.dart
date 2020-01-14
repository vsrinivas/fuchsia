// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
import 'dart:async';
import 'dart:ui';

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
import 'package:simple_browser/src/widgets/tabs_widget.dart';

const _emptyTitle = 'NEW TAB';

void main() {
  setupLogger(name: 'tabs_widget_test');

  TabsBloc tabsBloc;
  SimpleBrowserWebService mockWebService;
  SimpleBrowserNavigationEventListener mockListener;
  ValueNotifier titleNotifier = ValueNotifier<String>('');

  setUp(() {
    mockWebService = MockSimpleBrowserWebService();
    mockListener = MockSimpleBrowserNavigationEventListener();

    when(mockWebService.navigationEventListener)
        .thenAnswer((_) => mockListener);
    when(mockListener.pageTitleNotifier).thenAnswer((_) => titleNotifier);

    tabsBloc = TabsBloc(
      tabFactory: () => WebPageBloc(
        webService: mockWebService,
      ),
      disposeTab: (tab) => tab.dispose(),
    );
  });

  testWidgets('Should create tab widgets when more than two tabs added.',
      (WidgetTester tester) async {
    // Initial state: Tabsbloc does not have any tabs.
    expect(tabsBloc.tabs.length, 0);
    await _setUpTabsWidget(tester, tabsBloc);

    // Sees if there is no tab widgets on the screen.
    expect(_findNewTabWidgets(), findsNothing,
        reason: 'Expected no tab widgets when no tabs added.');

    // Adds one tab and sees if there is a tab in the tabsBloc, but still no tab widgets
    // on the screen.
    await _addNTabsToTabsBloc(tester, tabsBloc, 1);
    expect(_findNewTabWidgets(), findsNothing,
        reason: 'Expected no tab widgets when only 1 tab added.');

    // Adds one more tab and sees if there are two tabs in the tabsBloc.
    await _addNTabsToTabsBloc(tester, tabsBloc, 1);

    // Sees if there are two Expanded widgets for the tab widgets.
    // A tabsBloc should be wrapped by an Expanded widget when the total widths of
    // the currently displayed tabs is smaller than the browser width.
    expect(find.byType(Expanded), findsNWidgets(2),
        reason: 'Expected 2 Expanded widgets when 2 tabs added.');

    // See if the tab widgets have the default title.
    expect(_findNewTabWidgets(), findsNWidgets(2),
        reason: '''Expected 2 tab widgets with the title, $_emptyTitle,
        when 2 tabs added.''');
  });

  testWidgets(
      'Should create tab widgets with a custom title when one is given.',
      (WidgetTester tester) async {
    // Gives a custom title, 'TAB_WIDGET_TEST'.
    String expectedTitle = 'TAB_WIDGET_TEST';
    when(mockListener.pageTitle).thenReturn(expectedTitle);

    await _setUpTabsWidget(tester, tabsBloc);

    // Adds two tabs since tab widgets are only created when there are more than one tab.
    await _addNTabsToTabsBloc(tester, tabsBloc, 2);

    // Sees if there are two tab widgets with the given title.
    expect(find.text(expectedTitle), findsNWidgets(2),
        reason: '''Expected 2 tab widgets with the title $expectedTitle
        when 2 new tabs with it have been added.''');
  });

  testWidgets('''Should give the minimum width to the tab widgets
      when the total sum of the tab widths is larger than the browser width.''',
      (WidgetTester tester) async {
    await _setUpTabsWidget(tester, tabsBloc);

    // Adds seven tabs.
    await _addNTabsToTabsBloc(tester, tabsBloc, 7);

    // Sees if there are seven SizedBox widgets with the minimum width for the tab widgets.
    // A tabsBloc should be wrapped by a SizedBox widget and have the minimum width
    // when the total widths of the currently displayed tabs is larger than the browser width.
    expect(_findMinTabWidgets(), findsNWidgets(7));
  });

  testWidgets('Should change the focus to it when an unfocused tab is tapped.',
      (WidgetTester tester) async {
    await _setUpTabsWidget(tester, tabsBloc);

    await _addNTabsToTabsBloc(tester, tabsBloc, 3);

    expect(tabsBloc.currentTabIdx, 2,
        reason: 'Expected the 3rd tab widget was focused by default.');

    final tabs = _findNewTabWidgets();
    await tester.tap(tabs.at(0));
    await tester.pumpAndSettle();
    expect(tabsBloc.currentTabIdx, 0,
        reason: 'Expected the 1st tab widget is focused when tapped on it.');
  });

  testWidgets(
      'Should show close buttons on the focused tab and a hovered tab if any.',
      (WidgetTester tester) async {
    await _setUpTabsWidget(tester, tabsBloc);
    await _addNTabsToTabsBloc(tester, tabsBloc, 3);
    final tabs = _findNewTabWidgets();
    expect(tabs, findsNWidgets(3),
        reason: 'Expected to find 3 tab widgets when 3 tabs added.');

    // Sees if there is only one tab that has a close button on it.
    expect(_findClose(), findsOneWidget,
        reason: 'Expected to find 1 close button when hovering no tabs.');

    // Configures a gesture.
    final TestGesture gesture =
        await tester.createGesture(kind: PointerDeviceKind.mouse);
    addTearDown(gesture.removePointer);

    // Mouse Enters onto the first tab.
    final Offset firstTabCenter = tester.getCenter(tabs.at(0));
    await gesture.moveTo(firstTabCenter);
    await tester.pumpAndSettle();

    // Sees if there are two tabs that have a close button on it.
    expect(_findClose(), findsNWidgets(2),
        reason: 'Expected to find 2 close buttons when hovering a tab.');

    // Mouse is out from the first tab and moves to the currently focused tab.
    final Offset focusedTabCenter =
        tester.getCenter(tabs.at(tabsBloc.currentTabIdx));
    await gesture.moveTo(focusedTabCenter);
    await tester.pumpAndSettle();

    // Sees if there is only one tab that has a close button on it.
    expect(_findClose(), findsOneWidget,
        reason:
            'Expected to find 1 close button when hovering the focused tab.');
  });

  testWidgets('Should close the tab when its close button is tapped.',
      (WidgetTester tester) async {
    await _setUpTabsWidget(tester, tabsBloc);
    await _addNTabsToTabsBloc(tester, tabsBloc, 4);
    expect(_findNewTabWidgets(), findsNWidgets(4),
        reason: 'Expected to find 4 tab widgets when 4 tabs added.');
    expect(tabsBloc.currentTabIdx, 3,
        reason: 'Expected the 4th tab widget was focused by default.');

    Finder closeButtons = _findClose();
    expect(closeButtons, findsOneWidget,
        reason: 'Expected to find 1 close button when hovering no tabs.');

    await tester.tap(closeButtons);
    await tester.pumpAndSettle();
    expect(tabsBloc.tabs.length, 3,
        reason: 'Expected 3 tabs in tabsBloc after tapped the close.');
    expect(_findNewTabWidgets(), findsNWidgets(3),
        reason: 'Expected to find 3 tab widgets after tapped the close.');
    expect(tabsBloc.currentTabIdx, 2,
        reason:
            'Expected the 3rd tab widget was focused after tapped the close.');
  });

  testWidgets(
      'The tab widget list should scroll if needed depending on the offset of the selected tab.',
      (WidgetTester tester) async {
    final rightScrollMargin = 800.0 - kScrollToMargin - kMinTabWidth;
    final rightMinMargin = 800.0 - kMinTabWidth;
    final leftScrollMargin = kScrollToMargin;
    const leftMinMargin = 0.0;

    await _setUpTabsWidget(tester, tabsBloc);

    // Creates 8 tabs.
    await _addNTabsToTabsBloc(tester, tabsBloc, 8);

    // The screen(container) width: 800.0
    // The total width of the tab widgets: 120.0 * 8 = 960.
    // The expected display of the tab widgets (* is the currently focused tab):
    // |- 0 -| |- 1 -| |- 2 -| |- 3 -| |- 4 -| |- 5 -| |- 6 -| |- 7* -|
    //          |-                     SCREEN                        -|

    // See if the last tab is currenly focused.
    expect(tabsBloc.currentTabIdx, 7,
        reason: 'Expected the 8th tab widget is focused by default.');

    final tabsOnScreen = _findMinTabWidgets();

    // Sees if there are 7 tab widgets on the screen.
    expect(tabsOnScreen, findsNWidgets(7),
        reason: 'Expected 7 out of 8 tab widgets on the 800-wide viewport.');

    final lastTab = tabsOnScreen.last;
    final fixedY = tester.getTopLeft(lastTab).dy;
    final expectedLastPosition = Offset(rightMinMargin, fixedY);

    expect(tester.getTopLeft(lastTab), expectedLastPosition,
        reason: '''Expected the initial X position of the last tab widget
        to be $expectedLastPosition.''');

    // The fifth tab in the tabsBloc is the forth tab widget on the current screen.
    final fifthTab = tabsOnScreen.at(3);
    final expectedFifthPosition = tester.getTopLeft(fifthTab);

    // Taps on the fifth tab to change the focus.
    await tester.tap(fifthTab);
    await tester.pumpAndSettle();

    // The expected display of the tab widgets (* is the currently focused tab):
    // |- 0 -| |- 1 -| |- 2 -| |- 3 -| |- 4* -| |- 5 -| |- 6 -| |- 7 -|
    //          |-                     SCREEN                        -|

    expect(tabsBloc.currentTabIdx, 4,
        reason: 'Expected the 5th tab to be focused when tapped on it.');
    expect(tester.getTopLeft(fifthTab), expectedFifthPosition,
        reason: '''Expected the tab widget list stay still when tapped on
        the 5th tab, which is the 4th tab widget on the screen.''');

    // The second tab in the tabsBloc is the first tab widget on the current screen.
    final secondTab = tabsOnScreen.at(0);

    final expectedSecondPosition = Offset(leftScrollMargin, fixedY);
    await tester.tap(secondTab);
    await tester.pumpAndSettle();

    // The expected display of the tab widgets (* is the currently focused tab):
    //      |- 0 -| |- 1* -| |- 2 -| |- 3 -| |- 4 -| |- 5 -| |- 6 -| |- 7 -|
    //          |-                     SCREEN                        -|

    expect(tabsOnScreen, findsNWidgets(8),
        reason: '''Expected 8 tab widgets on the screen when tapped on the
        2nd tab, which was the 1st tab widget on the screen.''');
    expect(tabsBloc.currentTabIdx, 1,
        reason: '''Expected the 2nd tab to be focused when tapped on it.''');
    expect(tester.getTopLeft(tabsOnScreen.at(1)), expectedSecondPosition,
        reason: '''Expected the tab widget list to shift from left to right
        and the 2nd tab widget to be fully revealed on the screen when
        tapped on it.''');

    final expectedFirstPosition = Offset(leftMinMargin, fixedY);
    // Directly adds the FocusTabAction to the tabsBloc since tester.tap() does not
    // work on the widget whose center is offscreen.
    tabsBloc.request.add(FocusTabAction(tab: tabsBloc.tabs[0]));
    await tester.pumpAndSettle();

    // The expected display of the tab widgets (* is the currently focused tab):
    //          |- 0* -| |- 1 -| |- 2 -| |- 3 -| |- 4 -| |- 5 -| |- 6 -| |- 7 -|
    //          |-                     SCREEN                        -|

    expect(tabsOnScreen, findsNWidgets(7),
        reason:
            'Expected 7 tab widgets on the screen when 1st tab is focused.');

    expect(tabsBloc.currentTabIdx, 0,
        reason: 'Expected the 1st tab to be focused when moved focus on it.');
    expect(tester.getTopLeft(tabsOnScreen.at(0)), expectedFirstPosition,
        reason: '''Expected the tab widget list to shift from left to right
            and the 1st tab widget to be fully revealed on the screen when
            tapped on it.''');

    final seventhTab = tabsOnScreen.at(6);
    final expectedSeventhPosition = Offset(rightScrollMargin, fixedY);
    await tester.tap(seventhTab);
    await tester.pumpAndSettle();

    // The expected display of the tab widgets (* is the currently focused tab):
    //     |- 0 -| |- 1 -| |- 2 -| |- 3 -| |- 4 -| |- 5 -| |- 6* -| |- 7 -|
    //          |-                     SCREEN                        -|

    expect(tabsOnScreen, findsNWidgets(8),
        reason: '''Expected 8 tab widgets on the screen when tapped on the
        7th tab widget.''');

    expect(tabsBloc.currentTabIdx, 6,
        reason: '''Expected the 7th tab to be focused when tapped on it.''');
    expect(tester.getTopLeft(seventhTab), expectedSeventhPosition,
        reason: '''Expected the tab widget list to shift from right to left
            and the 7th tab widget to be fully revealed on the screen when
            tapped on it.''');
  });
}

Future<void> _setUpTabsWidget(WidgetTester tester, TabsBloc tabsBloc) async {
  await tester.pumpWidget(MaterialApp(
    home: Scaffold(
      body: Container(
        width: 800,
        child: TabsWidget(
          bloc: tabsBloc,
        ),
      ),
    ),
  ));

  await tester.pumpAndSettle();
}

Future<void> _addNTabsToTabsBloc(
    WidgetTester tester, TabsBloc tabsBloc, int n) async {
  int currentNumTabs = tabsBloc.tabs.length;
  for (int i = 0; i < n; i++) {
    tabsBloc.request.add(NewTabAction());
    await tester.pumpAndSettle();
  }
  expect(tabsBloc.tabs.length, currentNumTabs + n);
}

Finder _findMinTabWidgets() => find.byWidgetPredicate(
    (Widget widget) => widget is SizedBox && (widget.width) == kMinTabWidth);

Finder _findNewTabWidgets() => find.text(_emptyTitle);

Finder _findClose() => find.text(kCloseMark);

class MockSimpleBrowserNavigationEventListener extends Mock
    implements SimpleBrowserNavigationEventListener {}

class MockSimpleBrowserWebService extends Mock
    implements SimpleBrowserWebService {}

class MockWebPageBloc extends Mock implements WebPageBloc {}
