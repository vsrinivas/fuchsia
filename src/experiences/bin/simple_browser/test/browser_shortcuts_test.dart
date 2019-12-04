// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:fidl_fuchsia_ui_shortcut/fidl_async.dart' as ui_shortcut
    show RegistryProxy;

import 'package:flutter/material.dart';

import 'package:fuchsia_logger/logger.dart';

import 'package:mockito/mockito.dart';
import 'package:test/test.dart';

// ignore_for_file: implementation_imports
import 'package:simple_browser/src/blocs/tabs_bloc.dart';
import 'package:simple_browser/src/blocs/webpage_bloc.dart';
import 'package:simple_browser/src/utils/browser_shortcuts.dart';

void main() {
  MockRegistryProxy mockRegistryProxy;
  MockTabsBloc<WebPageBloc> mockTabsBloc;
  const List<String> defaultKeys = [
    'newTab',
    'closeTab',
    'goBack',
    'goForward',
    'refresh',
    'previousTab',
    'nextTab'
  ];

  setupLogger(name: 'browser_shortcuts_test');

  setUp(() {
    mockRegistryProxy = MockRegistryProxy();
    mockTabsBloc = MockTabsBloc<WebPageBloc>();
  });

  test('''Should use the default action map
    when there is no action input for BrowserShortcuts''', () {
    BrowserShortcuts bs = BrowserShortcuts(
      tabsBloc: mockTabsBloc, //tabsBloc,
      shortcutRegistry: mockRegistryProxy,
    );

    expect(bs.actions.length, 7, reason: '''Expected 7 shortcuts,
        but actually ${bs.actions.length} shortcuts.''');

    for (String key in defaultKeys) {
      expect(bs.actions.containsKey(key), true,
          reason: 'Expected to have $key, but does not.');
    }
  });

  test('Should use the input action map when it is given for BrowserShortcuts',
      () {
    Map<String, VoidCallback> testActions = {
      'action1': () {},
      'action2': () {},
      'action3': () {},
    };
    BrowserShortcuts bs = BrowserShortcuts(
      tabsBloc: mockTabsBloc, //tabsBloc,
      shortcutRegistry: mockRegistryProxy,
      actions: testActions,
    );

    expect(bs.actions.length, 3, reason: '''Expected 3 shortcuts,
            but actually ${bs.actions.length} shortcuts.''');
    testActions.forEach((key, value) {
      expect(bs.actions.containsKey(key), true,
          reason: 'Expected to have $key, but does not.');
    });
    for (String key in defaultKeys) {
      expect(bs.actions.containsKey(key), false,
          reason: 'Expected not to have $key, but does.');
    }
  });
}

class MockRegistryProxy extends Mock implements ui_shortcut.RegistryProxy {}

class MockTabsBloc<WebPageBloc> extends Mock implements TabsBloc<WebPageBloc> {}
