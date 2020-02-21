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
const screenWidth = 800.0;

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

    _verifyFocusedTabIndex(tabsBloc, 2);

    final tabs = _findNewTabWidgets();
    await tester.tap(tabs.at(0));
    await tester.pumpAndSettle();

    _verifyFocusedTabIndex(tabsBloc, 0);
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
    _verifyFocusedTabIndex(tabsBloc, 3);

    Finder closeButtons = _findClose();
    expect(closeButtons, findsOneWidget,
        reason: 'Expected to find 1 close button when hovering no tabs.');

    await tester.tap(closeButtons);
    await tester.pumpAndSettle();
    expect(tabsBloc.tabs.length, 3,
        reason: 'Expected 3 tabs in tabsBloc after tapped the close.');
    expect(_findNewTabWidgets(), findsNWidgets(3),
        reason: 'Expected to find 3 tab widgets after tapped the close.');
    _verifyFocusedTabIndex(tabsBloc, 2);
  });

  testWidgets(
      'Should rearrange the tabs when a tab is dragged to another position',
      (WidgetTester tester) async {
    await _setUpTabsWidget(tester, tabsBloc);
    await _addNTabsToTabsBloc(tester, tabsBloc, 5);
    final tabs = _findNewTabWidgets();
    expect(tabs, findsNWidgets(5),
        reason: 'Expected to find 5 tab widgets when 5 tabs added.');
    _verifyFocusedTabIndex(tabsBloc, 4);

    WebPageBloc originalTab0 = tabsBloc.tabs[0];
    WebPageBloc originalTab1 = tabsBloc.tabs[1];
    WebPageBloc originalTab2 = tabsBloc.tabs[2];
    WebPageBloc originalTab3 = tabsBloc.tabs[3];
    WebPageBloc originalTab4 = tabsBloc.tabs[4];

    // Drags the last tab 161.0 to the left.
    await tester.drag(tabs.at(4), Offset(-161.0, 0.0));
    await tester.pumpAndSettle();

    // Sees if the tab just moved is focused.
    expect(tabsBloc.currentTabIdx, 3,
        reason: '''Expected the index of the focused tab to be rearranged to 3,
            but actually has been rearranged to ${tabsBloc.currentTabIdx}.''');

    // Sees if all tabs have been rarranged correctly.
    expect(tabsBloc.tabs[0], originalTab0,
        reason: 'Expected that the 1st tab used to be the 1st.');
    expect(tabsBloc.tabs[1], originalTab1,
        reason: 'Expected that the 2nd tab used to be the 2nd.');
    expect(tabsBloc.tabs[2], originalTab2,
        reason: 'Expected that the 3rd tab used to be the 3rd.');
    expect(tabsBloc.tabs[3], originalTab4,
        reason: 'Expected that the 4th tab used to be the 5th.');

    expect(tabsBloc.tabs[4], originalTab3,
        reason: 'Expected that the 5th tab used to the 4th.');

    // Drags the 2nd tab 170.0 to the right.
    await tester.drag(tabs.at(1), Offset(161.0, 0.0));
    await tester.pumpAndSettle();

    // Sees if the tab just moved is focused.
    _verifyFocusedTabIndex(tabsBloc, 2);

    // Sees if all tabs have been rarranged correctly.
    expect(tabsBloc.tabs[0], originalTab0,
        reason: 'Expected that the 1st tab used to be the 1st.');
    expect(tabsBloc.tabs[1], originalTab2,
        reason: 'Expected that the 2nd tab used to be the 3rd.');
    expect(tabsBloc.tabs[2], originalTab1,
        reason: 'Expected that the 3rd tab used to be the 2nd.');
    expect(tabsBloc.tabs[3], originalTab4,
        reason: 'Expected that the 4th tab used to be the 5th.');
    expect(tabsBloc.tabs[4], originalTab3,
        reason: 'Expected that the 5th tab used to the 4th.');
  });

  group('Scrollable tab list', () {
    final rightScrollMargin =
        screenWidth - kTabBarHeight - kScrollToMargin - kMinTabWidth;
    final rightMinMargin = screenWidth - kTabBarHeight - kMinTabWidth;
    final leftScrollMargin = kScrollToMargin + kTabBarHeight;
    final leftMinMargin = kTabBarHeight;

    testWidgets('The tab widget list should scroll on a scroll button tapped.',
        (WidgetTester tester) async {
      await _setUpTabsWidget(tester, tabsBloc);

      // Creates 8 tabs.
      await _addNTabsToTabsBloc(tester, tabsBloc, 10);

      // The expected display of the initial tab list (*currently focused tab):
      // |-0-||-1-||-2-||-3-||-4-||-5-||-6-||-7-||-8-||-9*-|
      //                   |-            SCREEN           -|

      final tabs = _findMinTabWidgets();

      // Sees if there are 8 tab widgets created.
      expect(tabs, findsNWidgets(10),
          reason: 'Expected to find 10 tabs, but actually $tabs were found.');

      // Sees if the viewport of the list is on the right.
      _verifyFocusedTabIndex(tabsBloc, 9);
      _verifyFocusedTabMargin(tester, tabs, rightMinMargin);
      _verifyPartlyOffscreenFromLeft(tester, tabs.at(3));

      final leftScrollButton = find.byIcon(Icons.keyboard_arrow_left);
      expect(leftScrollButton, findsOneWidget);

      final rightScrollButton = find.byIcon(Icons.keyboard_arrow_right);
      expect(rightScrollButton, findsOneWidget);

      // Taps on the left scroll button.
      await tester.tap(leftScrollButton);
      await tester.pumpAndSettle();

      // The expected display of the tab list (*currently focused tab):
      // |-0-||-1-||-2-||-3-||-4-||-5-||-6-||-7-||-8-||-9*-|
      //  |-            SCREEN           -|

      // Sees if the list has been scrolled to the left.
      _verifyPartlyOffscreenFromLeft(tester, tabs.first);
      _verifyEntirelyOffscreenFromRight(tester, tabs.last);
      _verifyPartlyOffscreenFromRight(tester, tabs.at(6));

      await tester.tap(leftScrollButton);
      await tester.pumpAndSettle();

      // The expected display of the tab list (*currently focused tab):
      // |-0-||-1-||-2-||-3-||-4-||-5-||-6-||-7-||-8-||-9*-|
      // |-            SCREEN           -|

      // Sees if the list has been scrolled to the left end.
      final firstTabLeftX = tester.getTopLeft(tabs.first).dx;
      expect(firstTabLeftX, leftMinMargin,
          reason: '''Expected the scroll list have been scroll to its far left,
          but actually it has not.''');

      // Taps on the right scroll button.
      await tester.tap(rightScrollButton);
      await tester.pumpAndSettle();

      // The expected display of the tab list (*currently focused tab):
      // |-0-||-1-||-2-||-3-||-4-||-5-||-6-||-7-||-8-||-9*-|
      //                  |-            SCREEN           -|

      // Sees if the list has been scrolled to the right.
      _verifyPartlyOffscreenFromLeft(tester, tabs.at(3));
      _verifyPartlyOffscreenFromRight(tester, tabs.last);

      await tester.tap(rightScrollButton);
      await tester.pumpAndSettle();

      // The expected display of the tab list (*currently focused tab):
      // |-0-||-1-||-2-||-3-||-4-||-5-||-6-||-7-||-8-||-9*-|
      //                   |-            SCREEN           -|

      // Sees if the list has been scrolled to the right end.
      _verifyFocusedTabMargin(tester, tabs, rightMinMargin);
      _verifyPartlyOffscreenFromLeft(tester, tabs.at(3));
    });

    testWidgets(
        'The tab widget list should scroll if needed depending on the offset of the focused tab.',
        (WidgetTester tester) async {
      await _setUpTabsWidget(tester, tabsBloc);

      // Creates 8 tabs.
      await _addNTabsToTabsBloc(tester, tabsBloc, 8);

      // The expected display of the initial tab list (*currently focused tab):
      // |- 0 -| |- 1 -| |- 2 -| |- 3 -| |- 4 -| |- 5 -| |- 6 -| |- 7* -|
      //              |-                     SCREEN                    -|

      final tabs = _findMinTabWidgets();

      // Sees if there are 8 tab widgets created.
      expect(tabs, findsNWidgets(8),
          reason: 'Expected to find 8 tabs, but actually $tabs were found.');

      // Sees if the expected tab is currently focused and its at the desired
      // position.
      _verifyFocusedTabIndex(tabsBloc, 7);
      _verifyFocusedTabMargin(tester, tabs, rightMinMargin);

      // Sees if certain tabs are partly/entire offscreen.
      _verifyEntirelyOffscreenFromLeft(tester, tabs.first);
      _verifyPartlyOffscreenFromLeft(tester, tabs.at(1));

      // Finds the fifth tab and checks its current position.
      final fifthTab = tabs.at(4);
      final expectedFifthTabLeftX = tester.getTopLeft(fifthTab).dx;

      // Taps on the fifth tab to change the focus.
      await tester.tap(fifthTab);
      await tester.pumpAndSettle();

      // The expected display of the tab list (*currently focused tab):
      // |- 0 -| |- 1 -| |- 2 -| |- 3 -| |- 4* -| |- 5 -| |- 6 -| |- 7 -|
      //              |-                     SCREEN                    -|

      _verifyFocusedTabIndex(tabsBloc, 4);
      _verifyFocusedTabMargin(tester, tabs, expectedFifthTabLeftX);

      // Focuses on the second tab by directly adding the FocusTabAction to the
      // tabsBloc since tester.tap() does not work on the widget whose center
      // is offscreen.
      tabsBloc.request.add(FocusTabAction(tab: tabsBloc.tabs[1]));
      await tester.pumpAndSettle();

      // The expected display of the tab list (*currently focused tab):
      // |- 0 -| |- 1* -| |- 2 -| |- 3 -| |- 4 -| |- 5 -| |- 6 -| |- 7 -|
      //     |-                     SCREEN                    -|

      _verifyFocusedTabIndex(tabsBloc, 1);
      _verifyFocusedTabMargin(tester, tabs, leftScrollMargin);

      _verifyPartlyOffscreenFromLeft(tester, tabs.first);
      _verifyPartlyOffscreenFromRight(tester, tabs.at(5));
      _verifyEntirelyOffscreenFromRight(tester, tabs.at(6));

      // Focuses on the first tab.
      tabsBloc.request.add(FocusTabAction(tab: tabsBloc.tabs[0]));
      await tester.pumpAndSettle();

      // The expected display of the tab list (*currently focused tab):
      // |- 0* -| |- 1 -| |- 2 -| |- 3 -| |- 4 -| |- 5 -| |- 6 -| |- 7 -|
      // |-                     SCREEN                    -|

      _verifyFocusedTabIndex(tabsBloc, 0);
      _verifyFocusedTabMargin(tester, tabs, leftMinMargin);

      _verifyPartlyOffscreenFromRight(tester, tabs.at(5));
      _verifyEntirelyOffscreenFromRight(tester, tabs.at(6));

      // Focuses on the seventh tab.
      tabsBloc.request.add(FocusTabAction(tab: tabsBloc.tabs[6]));
      await tester.pumpAndSettle();

      // The expected display of the tab list (*currently focused tab):
      // |- 0 -| |- 1 -| |- 2 -| |- 3 -| |- 4 -| |- 5 -| |- 6* -| |- 7 -|
      //          |-                     SCREEN                    -|

      _verifyFocusedTabIndex(tabsBloc, 6);
      _verifyFocusedTabMargin(tester, tabs, rightScrollMargin);

      _verifyEntirelyOffscreenFromLeft(tester, tabs.first);
      _verifyPartlyOffscreenFromLeft(tester, tabs.at(1));
      _verifyPartlyOffscreenFromRight(tester, tabs.at(6));
    });
  });
}

Future<void> _setUpTabsWidget(WidgetTester tester, TabsBloc tabsBloc) async {
  await tester.pumpWidget(MaterialApp(
    home: Scaffold(
      body: Container(
        width: screenWidth,
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

Finder _findMinTabWidgets() => find.byWidgetPredicate((Widget widget) {
      if (widget is Container && widget.key == Key('tab')) {
        BoxConstraints width = widget.constraints.widthConstraints();
        return (width.minWidth == width.maxWidth) &&
            (width.minWidth == kMinTabWidth);
      }
      return false;
    });

Finder _findNewTabWidgets() => find.text(_emptyTitle);

Finder _findClose() => find.byIcon(Icons.clear);

// Verifies if the currently focused tab is the expected tab.
void _verifyFocusedTabIndex(TabsBloc tb, int expectedIndex) {
  expect(tb.currentTabIdx, expectedIndex,
      reason: '''Expected the index of the currently focused tab to be
      $expectedIndex, but actually is ${tb.currentTabIdx}''');
}

// Verifies if the currently focused tab has the expected left margin.
void _verifyFocusedTabMargin(
    WidgetTester tester, Finder tabs, double expectedMargin) {
  // The currently focused tab always become the last member of the finder
  // since it is rendered on the top of the other tabs.
  final actualMargin = tester.getTopLeft(tabs.last).dx;
  expect(actualMargin, expectedMargin,
      reason: '''Expected the left margin to the currently focused tab to be
        $expectedMargin, but is actually $actualMargin.''');
}

void _verifyPartlyOffscreenFromLeft(WidgetTester tester, Finder tab) {
  final leftBorderX = kTabBarHeight;
  final tabLeftX = tester.getTopLeft(tab).dx;
  final tabRightX = tester.getTopRight(tab).dx;
  expect(tabLeftX < leftBorderX, true,
      reason: '''Expected this tab's left edge to be offscreen,
          but actually is onscreen.''');
  expect(tabRightX >= leftBorderX, true,
      reason: '''Expected this tab's right edge to be onscreen,
      but actually is offscreen.''');
}

void _verifyEntirelyOffscreenFromLeft(WidgetTester tester, Finder tab) {
  final leftBorderX = kTabBarHeight;
  final tabRightX = tester.getTopRight(tab).dx;
  expect(tabRightX < leftBorderX, true,
      reason: '''Expected this tab's right edge to be offscreen,
      but actually is onscreen.''');
}

void _verifyPartlyOffscreenFromRight(WidgetTester tester, Finder tab) {
  final rightBorderX = screenWidth - kTabBarHeight;
  final tabLeftX = tester.getTopLeft(tab).dx;
  final tabRightX = tester.getTopRight(tab).dx;
  expect(tabLeftX < rightBorderX, true,
      reason: '''Expected this tab's left edge to be onscreen,
          but actually is offscreen.''');
  expect(tabRightX >= rightBorderX, true,
      reason: '''Expected this tab's right edge to be offscreen,
      but actually is onscreen.''');
}

void _verifyEntirelyOffscreenFromRight(WidgetTester tester, Finder tab) {
  final rightBorderX = screenWidth - kTabBarHeight;
  final tabLeftX = tester.getTopLeft(tab).dx;
  expect(tabLeftX > rightBorderX, true,
      reason: '''Expected this tab's left edge to be offscreen,
      but actually is onscreen.''');
}

class MockSimpleBrowserNavigationEventListener extends Mock
    implements SimpleBrowserNavigationEventListener {}

class MockSimpleBrowserWebService extends Mock
    implements SimpleBrowserWebService {}

class MockWebPageBloc extends Mock implements WebPageBloc {}
