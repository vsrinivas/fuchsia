// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:flutter/material.dart';
import 'package:flutter_localizations/flutter_localizations.dart';
import 'package:fuchsia_scenic_flutter/child_view.dart' show ChildView;
import 'package:internationalization/localizations_delegate.dart'
    as localizations;
import 'package:internationalization/strings.dart';
import 'package:internationalization/supported_locales.dart'
    as supported_locales;
import 'package:intl/intl.dart';
import 'package:simple_browser/src/blocs/webpage_bloc.dart';
import 'src/models/app_model.dart';
import 'src/widgets/error_page.dart';
import 'src/widgets/navigation_bar.dart';
import 'src/widgets/tabs_widget.dart';

const _kBackgroundColor = Color(0xFFE5E5E5);
const _kForegroundColor = Color(0xFF191919);
const _kSelectionColor = Color(0x26191919);
const _kTextStyle = TextStyle(color: _kForegroundColor, fontSize: 14.0);

class App extends StatelessWidget {
  final AppModel model;

  const App(this.model);

  @override
  Widget build(BuildContext context) {
    return StreamBuilder<Locale>(
      stream: model.localeStream,
      initialData: model.initialLocale,
      builder: (context, snapshot) {
        final locale = snapshot.data;
        Intl.defaultLocale = locale.toString();
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
          localizationsDelegates: [
            localizations.delegate(),
            GlobalMaterialLocalizations.delegate,
            GlobalWidgetsLocalizations.delegate,
          ],
          supportedLocales: supported_locales.locales,
          home: Scaffold(
            body: Column(
              children: <Widget>[
                AnimatedBuilder(
                  animation: model.tabsBloc.currentTabNotifier,
                  builder: (_, __) => NavigationBar(
                      bloc: model.tabsBloc.currentTab, newTab: model.newTab),
                ),
                TabsWidget(bloc: model.tabsBloc),
                Expanded(child: _buildContent()),
              ],
            ),
          ),
        );
      },
    );
  }

  Widget _buildContent() => AnimatedBuilder(
        animation: model.tabsBloc.currentTabNotifier,
        builder: (_, __) => model.tabsBloc.currentTab == null
            // hide if no tab selected
            ? _buildEmptyPage()
            : AnimatedBuilder(
                animation: model.tabsBloc.currentTab.pageTypeNotifier,
                builder: (_, __) =>
                    _buildPage(model.tabsBloc.currentTab.pageType),
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
      ChildView(connection: model.tabsBloc.currentTab.childViewConnection);
  Widget _buildErrorPage() => ErrorPage();
  Widget _buildEmptyPage() => Container();
}
