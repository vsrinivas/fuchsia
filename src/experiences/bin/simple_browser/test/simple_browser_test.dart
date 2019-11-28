// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:fuchsia_logger/logger.dart';
import 'package:intl/intl.dart';
import 'package:mockito/mockito.dart';

// ignore_for_file: implementation_imports
import 'package:simple_browser/app.dart';
import 'package:simple_browser/src/blocs/tabs_bloc.dart';
import 'package:simple_browser/src/blocs/webpage_bloc.dart';
import 'package:simple_browser/src/models/app_model.dart';
import 'package:simple_browser/src/models/tabs_action.dart';

void main() {
  setupLogger(name: 'simple_browser_test');

  testWidgets('localized text is displayed in the widgets',
      (WidgetTester tester) async {
 
    final defaultLocale = Locale('sr', 'RS');
    Stream<Locale> lstream = Stream.fromIterable(
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

    final model = MockAppModel();
    when(model.tabsBloc).thenAnswer((_) => tabsBloc);
    when(model.initialLocale).thenReturn(defaultLocale);
    when(model.localeStream).thenAnswer((_) => lstream);

    final app = App(model);

    await tester.pumpWidget(app);
    expect(Intl.defaultLocale, defaultLocale.toString());
    await tester.pumpWidget(app);
    expect(
        find.byWidgetPredicate(
            (Widget widget) => widget is Title && widget.title == 'Прегледач',
            description: 'A widget with a localized title was displayed'),
        findsOneWidget);
  });
}

class MockAppModel extends Mock implements AppModel {}