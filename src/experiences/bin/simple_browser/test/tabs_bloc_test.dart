// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:async';
import 'package:flutter/foundation.dart';
import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:fuchsia_logger/logger.dart';
import 'package:mockito/mockito.dart';

// ignore_for_file: implementation_imports
import 'package:simple_browser/src/blocs/tabs_bloc.dart';
import 'package:simple_browser/src/blocs/webpage_bloc.dart';
import 'package:simple_browser/src/models/tabs_action.dart';

void main() {
  setupLogger(name: 'tabs_bloc_test');

  group('newTab', () {
    test('Add 1 tab.', () async {
      // Creates one tab.
      final tb = await _creatNTabs(1);

      // Sees if there is only one tab.
      int actual = tb.tabs.length;
      int expected = 1;
      expect(
        actual,
        expected,
        reason: _expectNTabsReason(
          actual.toString(),
          expected.toString(),
        ),
      );
    });

    test('Add 2 tabs.', () async {
      // Creates two tabs.
      final tb = await _creatNTabs(2);

      // Sees if there are two tabs.
      int actual = tb.tabs.length;
      int expected = 2;
      expect(
        actual,
        expected,
        reason: _expectNTabsReason(
          actual.toString(),
          expected.toString(),
        ),
      );
    });
  });

  group('focusTab', () {
    test('Add 3 tabs, focus on the first tab and then the second tab.',
        () async {
      // Test Environment Set Up:
      // Creates three tabs.
      final tb = await _creatNTabs(3);

      // Makes sure that the last tab is currently focused.
      int actual = tb.currentTabIdx;
      int expected = 2;
      expect(
        actual,
        expected,
        reason: _expectFocusedTabReason(
          actual.toString(),
          expected.toString(),
        ),
      );

      // Changes the focus to the first tab.
      await _focusTab(tb, tb.tabs[0]);

      // Sees if the index of the newly focused tab is 0.
      actual = tb.currentTabIdx;
      expected = 0;
      expect(
        actual,
        expected,
        reason: _expectFocusedTabReason(
          actual.toString(),
          expected.toString(),
        ),
      );

      // Changes the focus to the second tab.
      await _focusTab(tb, tb.tabs[1]);

      // Sees if the index of the newly focused tab is 1.
      actual = tb.currentTabIdx;
      expected = 1;
      expect(
        actual,
        expected,
        reason: _expectFocusedTabReason(
          actual.toString(),
          expected.toString(),
        ),
      );
    });
  });

  group('removeTab', () {
    test('Add 1 tab and remove it.', () async {
      // Test Environment Set Up:
      // Creates one tab.
      final tb = await _creatNTabs(1);

      // Makes sure that there is only one tab.
      int actual = tb.tabs.length;
      int expected = 1;
      expect(
        actual,
        expected,
        reason: _expectNTabsReason(
          actual.toString(),
          expected.toString(),
        ),
      );

      // Closes the tab.
      await _closeTab(tb, tb.currentTab);

      // Sees if there is no tabs anyumore.
      actual = tb.tabs.length;
      expected = 0;
      expect(
        actual,
        expected,
        reason: _expectNTabsReason(
          actual.toString(),
          expected.toString(),
        ),
      );

      // Sees if the currentTab indicates null.
      expect(
        tb.currentTab,
        null,
        reason: _expectFocusedTabReason(
          (tb.currentTabIdx).toString(),
          'null',
        ),
      );
    });

    test(
        'Add 3 tabs, and remove the currently focused tab, which is the last tab.',
        () async {
      // Test Environment Set Up:
      // Creates three tabs and focus on the first tab.
      final tb = await _creatNTabs(3);

      // Makes sure that the last tab is currently focused.
      int actualFocusedTabIdx = tb.currentTabIdx;
      int expectedFocusedTabIdx = 2;
      expect(
        actualFocusedTabIdx,
        expectedFocusedTabIdx,
        reason: _expectFocusedTabReason(
          actualFocusedTabIdx.toString(),
          expectedFocusedTabIdx.toString(),
        ),
      );

      // Saves the tab previous to the currently focused tab. (the second tab)
      final expectedTab = tb.tabs[1];

      // Closes the currently focused tab. (the last tab)
      await _closeTab(tb, tb.currentTab);

      // Sees if the number of the remaining tabs is 2.
      int actualNumTabs = tb.tabs.length;
      int expectedNumTabs = 2;
      expect(
        actualNumTabs,
        expectedNumTabs,
        reason: _expectNTabsReason(
          actualNumTabs.toString(),
          expectedNumTabs.toString(),
        ),
      );

      // Sees if the newly focused tab is the tab previous to the removed tab.
      expect(tb.currentTab, expectedTab,
          reason:
              '''The currently focused tab is expected to be the previous one to the removed one,
              but is actually not.''');

      // Sees if the index of the newly focused tab is still 1.
      actualFocusedTabIdx = tb.currentTabIdx;
      expectedFocusedTabIdx = 1;
      expect(
        actualFocusedTabIdx,
        expectedFocusedTabIdx,
        reason: _expectFocusedTabReason(
          actualFocusedTabIdx.toString(),
          expectedFocusedTabIdx.toString(),
        ),
      );
    });

    test('Add 3 tabs, focus on the first tab, and remove it.', () async {
      // Test Environment Set Up:
      // Creates three tabs and focus on the first tab.
      final tb = await _creatNTabs(3);
      await _focusTab(tb, tb.tabs[0]);

      // Makes sure that the first tab is currently focused.
      int actualFocusedTabIdx = tb.currentTabIdx;
      int expectedFocusedTabIdx = 0;
      expect(
        actualFocusedTabIdx,
        expectedFocusedTabIdx,
        reason: _expectFocusedTabReason(
          actualFocusedTabIdx.toString(),
          expectedFocusedTabIdx.toString(),
        ),
      );

      // Saves the tab next to the currently focused tab. (the second tab)
      final expectedTab = tb.tabs[1];

      // Closes the currently focused tab. (the first tab)
      await _closeTab(tb, tb.currentTab);

      // Sees if the number of the remaining tabs is 2.
      int actualNumTabs = tb.tabs.length;
      int expectedNumTabs = 2;
      expect(
        actualNumTabs,
        expectedNumTabs,
        reason: _expectNTabsReason(
          actualNumTabs.toString(),
          expectedNumTabs.toString(),
        ),
      );

      // Sees if the newly focused tab is the tab next to the closed tab.
      expect(tb.currentTab, expectedTab,
          reason:
              '''The currently focused tab is expected to be the next one to the removed one,
              but is actually not.''');

      // Sees if the index of the newly focused tab has been shifted to 0.
      actualFocusedTabIdx = tb.currentTabIdx;
      expectedFocusedTabIdx = 0;
      expect(
        actualFocusedTabIdx,
        expectedFocusedTabIdx,
        reason: _expectFocusedTabReason(
          actualFocusedTabIdx.toString(),
          expectedFocusedTabIdx.toString(),
        ),
      );
    });

    test('Add 3 tabs and remove the first one.', () async {
      // Test Environment Set Up:
      // Creates three tabs.
      final tb = await _creatNTabs(3);

      // Makes sure that the last tab is currently focused.
      int actualFocusedTabIdx = tb.currentTabIdx;
      int expectedFocusedTabIdx = 2;
      expect(
        actualFocusedTabIdx,
        expectedFocusedTabIdx,
        reason: _expectFocusedTabReason(
          actualFocusedTabIdx.toString(),
          expectedFocusedTabIdx.toString(),
        ),
      );

      // Saves the currently focused tab. (the last tab)
      final expectedTab = tb.currentTab;

      // Closes the first tab.
      await _closeTab(tb, tb.tabs[0]);

      // Sees if the number of the remaining tabs is 2.
      int actualNumTabs = tb.tabs.length;
      int expectedNumTabs = 2;
      expect(
        actualNumTabs,
        expectedNumTabs,
        reason: _expectNTabsReason(
          actualNumTabs.toString(),
          expectedNumTabs.toString(),
        ),
      );

      // Sees if the currently focused tab is still the same one.
      expect(tb.currentTab, expectedTab,
          reason:
              '''The focused tab is expected to be the same before and after closing the second tab,
              but is actually different.''');

      // Sees if the index of the currently focused tab has been shifted to 1.
      actualFocusedTabIdx = tb.currentTabIdx;
      expectedFocusedTabIdx = 1;
      expect(
        actualFocusedTabIdx,
        expectedFocusedTabIdx,
        reason: _expectFocusedTabReason(
          actualFocusedTabIdx.toString(),
          expectedFocusedTabIdx.toString(),
        ),
      );
    });
  });

  group('isOnlyTab, previousTab, and nextTab getters', () {
    test(
        '''isOnlyTab getter should return true when there is only one tab in the TabsBloc,
      and return false when another tab is added.''', () async {
      // Creates one tab.
      final tb = await _creatNTabs(1);

      // Sees if isOnlyTab getter returns true.
      expect(
        tb.isOnlyTab,
        true,
        reason:
            'isOnlyTab is expected to be true, but is actually ${tb.isOnlyTab.toString()}.',
      );

      // Creates one more tab.
      await _newTab(tb);

      // Sees if isOnlyTab getter returns false.
      expect(
        tb.isOnlyTab,
        false,
        reason:
            'isOnlyTab is expected to be false, but is actually ${tb.isOnlyTab.toString()}.',
      );
    });

    test(
        '''previousTab getter should return the previous tab to the currently focused tab,
    and the nextTab getter should return the next tab to the currently focused tab.''',
        () async {
      // Creates two tabs.
      final tb = await _creatNTabs(2);

      // Sees if the previousTab getter returns the first tab.
      expect(
        tb.previousTab,
        tb.tabs.first,
        reason: '''previousTab is expected to be the first tab,
        but is actually ${tb.tabs.indexOf(tb.previousTab)}th tab.''',
      );

      // Changes the focus to the first tab.
      await _focusTab(tb, tb.tabs[0]);

      // Sees if the nextTab getter returns the second(last) tab.
      expect(
        tb.nextTab,
        tb.tabs.last,
        reason: '''nextTab is expected to be the last tab,
        but is actually ${tb.tabs.indexOf(tb.nextTab)}th tab.''',
      );
    });
  });
}

Future<TabsBloc> _creatNTabs(int n) async {
  TabsBloc tb = TabsBloc(
    tabFactory: () => MockWebPageBloc(),
    disposeTab: (tab) => tab.dispose(),
  );

  for (int i = 0; i < n; i++) {
    await _newTab(tb);
  }
  return tb;
}

String _expectNTabsReason(String actual, String expected) =>
    'TabsBloc is expected to have $expected tabs, but actually has $actual tabs.';

String _expectFocusedTabReason(String actual, String expected) =>
    'The index of the currently focused tab is expected to be $expected, but is actually $actual.';

/// awaits for a single callback from a [Listenable]
Future _awaitListenable(Listenable listenable) {
  final c = Completer();
  Function l;
  l = () {
    c.complete();
    listenable.removeListener(l);
  };
  listenable.addListener(l);
  return c.future;
}

/// sends an [TabsAction] to [TabsBloc] and awaits for a callback in a [Listenable]
Future _addActionAndAwait(
  TabsBloc tb,
  Listenable listenable,
  TabsAction action,
) async {
  tb.request.add(action);
  await _awaitListenable(listenable);
}

/// adds a new tab to a [TabsBloc] and awaits completion with [TabsBloc.tabs]
Future _newTab(TabsBloc tb) => _addActionAndAwait(
      tb,
      tb.tabsNotifier,
      NewTabAction(),
    );

/// sets focus to a tab in [TabsBloc] and awaits completion with [TabsBloc.currentTab]
Future _focusTab(TabsBloc tb, WebPageBloc tab) => _addActionAndAwait(
      tb,
      tb.currentTabNotifier,
      FocusTabAction(tab: tab),
    );

Future _closeTab(TabsBloc tb, WebPageBloc tab) => _addActionAndAwait(
      tb,
      tb.tabsNotifier,
      RemoveTabAction(tab: tab),
    );

class MockWebPageBloc extends Mock implements WebPageBloc {}
