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

class App extends StatefulWidget {
  @override
  AppState createState() => AppState();
}

class AppState extends State<App> {
  final TabsBloc _tabsBloc = TabsBloc();

  AppState() {
    _tabsBloc.request.add(NewTabAction());
  }

  @override
  void dispose() {
    _tabsBloc.dispose();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    return MaterialApp(
      title: 'Browser',
      home: Scaffold(
        backgroundColor: Colors.white,
        body: Container(
          child: Column(
            children: <Widget>[
              TabsWidget(bloc: _tabsBloc),
              Expanded(
                child: AnimatedBuilder(
                  animation: _tabsBloc.currentTab,
                  builder: (_, __) => _buildContent(_tabsBloc.currentTab.value),
                ),
              )
            ],
          ),
        ),
      ),
    );
  }

  Widget _buildContent(WebPageBloc tab) => Column(
        children: <Widget>[
          NavigationBar(bloc: tab),
          Expanded(
            child: ChildView(
              connection: tab.childViewConnection,
            ),
          ),
        ],
      );
}
