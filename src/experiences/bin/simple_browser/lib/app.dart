// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:flutter/material.dart';
import 'package:fuchsia_scenic_flutter/child_view.dart' show ChildView;
import 'package:simple_browser/src/blocs/webpage_bloc.dart';
import 'src/blocs/tabs_bloc.dart';
import 'src/widgets/navigation_bar.dart';
import 'src/widgets/tabs_widget.dart';

class App extends StatelessWidget {
  final TabsBloc tabsBloc;

  const App({@required this.tabsBloc});

  @override
  Widget build(BuildContext context) {
    return MaterialApp(
      title: 'Browser',
      home: Scaffold(
        backgroundColor: Colors.white,
        body: Container(
          child: Column(
            children: <Widget>[
              TabsWidget(bloc: tabsBloc),
              Expanded(
                child: AnimatedBuilder(
                  animation: tabsBloc.currentTab,
                  builder: (_, __) => tabsBloc.currentTab.value == null
                      ? Offstage()
                      : _buildContent(tabsBloc.currentTab.value),
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
