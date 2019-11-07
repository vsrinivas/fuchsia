// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:async';
import 'package:flutter/foundation.dart';
import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:fuchsia_logger/logger.dart';

// ignore_for_file: implementation_imports
import 'package:simple_browser/main.dart' show Localized;
import 'package:simple_browser/src/blocs/tabs_bloc.dart';
import 'package:simple_browser/src/blocs/webpage_bloc.dart';
import 'package:simple_browser/src/models/tabs_action.dart';

void main() {
  setupLogger(name: 'simple_browser_test');

  // Add a tab, and see that there is 1 tab
  test('add 1 tab', () async {
    final tb = TabsBloc(
      tabFactory: () => 'a',
      disposeTab: (tab) => tab.dispose(),
    );
    await _newTab(tb);

    expect(tb.tabs.length, 1, reason: "doesn't have 1 tab");
  });

  // Add 2 tabs, see that there are 2 tabs and that focus is on the second
  test('add 2 tabs', () async {
    TabsBloc tb;
    tb = TabsBloc(
      tabFactory: () => 'tab ${tb.tabs.length}',
      disposeTab: (tab) => tab.dispose(),
    );
    await _newTab(tb);
    await _newTab(tb);

    expect(tb.tabs.length, 2, reason: "doesn't have 2 tabs");
    expect(
      tb.currentTab,
      tb.tabs.last,
      reason: 'not focused on new tab',
    );
  });

  // Add 2 tabs, focs on the first, see that the focus is on the first, and its value one is correct
  test('add 2 tabs and focus on first tab', () async {
    TabsBloc tb;
    tb = TabsBloc(
      tabFactory: () => 'tab ${tb.tabs.length}',
      disposeTab: (tab) => tab.dispose(),
    );
    await _newTab(tb);
    await _newTab(tb);
    await _focusTab(tb, tb.tabs.first);

    expect(
      tb.currentTab,
      tb.tabs.first,
      reason: 'not focused on first tab',
    );
    expect(tb.currentTab, 'tab 0', reason: 'unexpected tab content');
  });

  testWidgets('localized text is displayed in the widgets',
      (WidgetTester tester) async {
    var locales = Stream.fromIterable(
        [Locale.fromSubtags(languageCode: 'sr', countryCode: 'RS')]);
    TabsBloc<WebPageBloc> tabsBloc;
    tabsBloc = TabsBloc(
      tabFactory: () => WebPageBloc(
          popupHandler: (tab) =>
              tabsBloc.request.add(AddTabAction<WebPageBloc>(tab: tab))),
      disposeTab: (tab) {
        tab.dispose();
      },
    );
    await tester.pumpWidget(Localized(tabsBloc, locales));
    await tester.pump();
    expect(
        find.byWidgetPredicate(
            (Widget widget) => widget is Title && widget.title == 'Прегледач',
            description: 'A widget with a localized title was displayed'),
        findsOneWidget);
  });
}

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
Future _newTab<T>(TabsBloc<T> tb) => _addActionAndAwait(
      tb,
      tb.tabsNotifier,
      NewTabAction<T>(),
    );

/// sets focus to a tab in [TabsBloc] and awaits completion with [TabsBloc.currentTab]
Future _focusTab<T>(TabsBloc tb, T tab) => _addActionAndAwait(
      tb,
      tb.currentTabNotifier,
      FocusTabAction<T>(tab: tab),
    );
