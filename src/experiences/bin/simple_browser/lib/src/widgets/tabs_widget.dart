// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:flutter/material.dart';
import '../blocs/tabs_bloc.dart';
import '../blocs/webpage_bloc.dart';
import '../models/tabs_action.dart';

const double _kTabBarHeight = 16.0;

class TabsWidget extends StatelessWidget {
  final TabsBloc<WebPageBloc> bloc;
  const TabsWidget({@required this.bloc});

  @override
  Widget build(BuildContext context) {
    return AnimatedBuilder(
      animation: Listenable.merge([bloc.tabsNotifier, bloc.currentTabNotifier]),
      builder: (_, __) => bloc.tabs.length > 1
          ? Container(
              height: _kTabBarHeight,
              color: Theme.of(context).accentColor,
              padding: EdgeInsets.symmetric(vertical: 1.0),
              child: Row(
                children: _buildPageTabs(context: context)
                    .map((tab) => Expanded(child: tab, flex: 1))
                    .toList(),
              ),
            )
          : Offstage(),
    );
  }

  Iterable<Widget> _buildPageTabs({BuildContext context}) => bloc.tabs.map(
        (tab) => AnimatedBuilder(
          animation: tab.pageTitleNotifier,
          builder: (_, __) => _buildTab(
            context: context,
            title: tab.pageTitle ?? 'NEW TAB',
            selected: tab == bloc.currentTab,
            onSelect: () {
              bloc.request.add(FocusTabAction(tab: tab));
            },
          ),
        ),
      );

  Widget _buildTab({
    BuildContext context,
    String title,
    bool selected,
    VoidCallback onSelect,
    double width,
  }) {
    return GestureDetector(
      onTap: onSelect,
      child: Container(
        width: width,
        color: selected
            ? Theme.of(context).primaryColor
            : Theme.of(context).accentColor,
        padding: EdgeInsets.symmetric(horizontal: 4.0),
        child: Center(
          child: Text(
            title,
            maxLines: 1,
            overflow: TextOverflow.ellipsis,
            style: TextStyle(
              fontFamily: 'RobotoMono',
              fontSize: 11.0,
              color: selected
                  ? Theme.of(context).accentColor
                  : Theme.of(context).primaryColor,
            ),
          ),
        ),
      ),
    );
  }
}
