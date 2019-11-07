// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:flutter/material.dart';
import 'package:fuchsia_scenic_flutter/child_view.dart' show ChildView;
import 'package:internationalization/strings.dart';
import 'package:simple_browser/src/blocs/webpage_bloc.dart';
import 'src/blocs/tabs_bloc.dart';
import 'src/models/tabs_action.dart';
import 'src/widgets/error_page.dart';
import 'src/widgets/navigation_bar.dart';
import 'src/widgets/tabs_widget.dart';

const _kBackgroundColor = Color(0xFFE5E5E5);
const _kForegroundColor = Color(0xFF191919);
const _kSelectionColor = Color(0x26191919);
const _kTextStyle = TextStyle(color: _kForegroundColor, fontSize: 14.0);

class App extends StatelessWidget {
  final TabsBloc<WebPageBloc> tabsBloc;

  final Locale locale;

  final Iterable<LocalizationsDelegate<dynamic>> localizationsDelegates;

  final Iterable<Locale> supportedLocales;

  /// The [locale], [localizationsDelegates] and [supportedLocales] parameters
  /// are the same as in [MaterialApp].
  const App(
      {@required this.tabsBloc,
      this.locale,
      this.localizationsDelegates,
      this.supportedLocales});

  @override
  Widget build(BuildContext context) {
    return MaterialApp(
      title: Strings.browser,
      theme: ThemeData(
        fontFamily: 'RobotoMono',
        textSelectionColor: _kSelectionColor,
        textSelectionHandleColor: _kForegroundColor,
        hintColor: _kForegroundColor,
        cursorColor: _kForegroundColor,
        primaryColor: _kBackgroundColor,
        canvasColor: _kBackgroundColor,
        accentColor: _kForegroundColor,
        textTheme: TextTheme(
          body1: _kTextStyle,
          subhead: _kTextStyle,
        ),
      ),
      locale: locale,
      localizationsDelegates: localizationsDelegates,
      supportedLocales: supportedLocales,
      home: Scaffold(
        body: Column(
          children: <Widget>[
            AnimatedBuilder(
              animation: tabsBloc.currentTabNotifier,
              builder: (_, __) =>
                  NavigationBar(bloc: tabsBloc.currentTab, newTab: newTab),
            ),
            TabsWidget(bloc: tabsBloc),
            Expanded(child: _buildContent()),
          ],
        ),
      ),
    );
  }

  Widget _buildContent() => AnimatedBuilder(
        animation: tabsBloc.currentTabNotifier,
        builder: (_, __) => tabsBloc.currentTab == null
            // hide if no tab selected
            ? _buildEmptyPage()
            : AnimatedBuilder(
                animation: tabsBloc.currentTab.pageTypeNotifier,
                builder: (_, __) => _buildPage(tabsBloc.currentTab.pageType),
              ),
      );

  Widget _buildPage(PageType pageType) {
    switch (pageType) {
      case PageType.normal:
        return _buildNormalPage();
      case PageType.error:
        // show error for error state
        return _buildErrorPage();
      default:
        // hide if no content in tab
        return _buildEmptyPage();
    }
  }

  Widget _buildNormalPage() =>
      ChildView(connection: tabsBloc.currentTab.childViewConnection);
  Widget _buildErrorPage() => ErrorPage();
  Widget _buildEmptyPage() => Container();

  void newTab() => tabsBloc.request.add(NewTabAction<WebPageBloc>());
}
