// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:flutter/material.dart';
import 'package:fuchsia_scenic_flutter/child_view.dart' show ChildView;
import 'package:simple_browser/src/blocs/webpage_bloc.dart';
import 'src/blocs/tabs_bloc.dart';
import 'src/models/tabs_action.dart';
import 'src/widgets/navigation_bar.dart';
import 'src/widgets/tabs_widget.dart';

const _kBackgroundColor = Color(0xFFE5E5E5);
const _kForegroundColor = Color(0xFF191919);
const _kSelectionColor = Color(0x26191919);

class App extends StatelessWidget {
  final TabsBloc<WebPageBloc> tabsBloc;

  const App({@required this.tabsBloc});

  @override
  Widget build(BuildContext context) {
    return MaterialApp(
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
          body1: TextStyle(color: _kForegroundColor),
          subhead: TextStyle(color: _kForegroundColor),
        ),
      ),
      title: 'Browser',
      home: Scaffold(
        body: Container(
          child: Column(
            children: <Widget>[
              AnimatedBuilder(
                animation: tabsBloc.currentTabNotifier,
                builder: (_, __) =>
                    NavigationBar(bloc: tabsBloc.currentTab, newTab: newTab),
              ),
              TabsWidget(bloc: tabsBloc),
              Expanded(
                child: _buildContent(),
              ),
            ],
          ),
        ),
      ),
    );
  }

  Widget _buildContent() => AnimatedBuilder(
        // hide if no tabs
        animation: tabsBloc.currentTabNotifier,
        builder: (_, __) => tabsBloc.currentTab != null
            ? AnimatedBuilder(
                // hide if no content in tab
                animation: tabsBloc.currentTab.urlNotifier,
                builder: (_, __) => tabsBloc.currentTab.url.isNotEmpty
                    ? ChildView(
                        connection: tabsBloc.currentTab.childViewConnection,
                      )
                    : Container(),
              )
            : Container(),
      );

  void newTab() {
    tabsBloc.request.add(NewTabAction<WebPageBloc>());
  }
}
