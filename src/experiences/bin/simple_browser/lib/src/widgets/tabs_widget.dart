// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:flutter/material.dart';
import '../blocs/tabs_bloc.dart';
import '../blocs/webpage_bloc.dart';
import '../models/tabs_action.dart';

const double _kTabBarHeight = 24.0;

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
                children: _buildPageTabs(context: context).toList(),
              ),
            )
          : Offstage(),
    );
  }

  Iterable<Widget> _buildPageTabs({BuildContext context}) => bloc.tabs
      .map(_buildTab)
      // add a 1pip separator before every tab,
      // divide the rest of the space between tabs
      .expand((item) => [
            SizedBox(width: 1),
            Expanded(child: item, flex: 1),
          ])
      // skip the first separator
      .skip(1);

  Widget _buildTab(
    WebPageBloc tab,
  ) =>
      _TabWidget(
        bloc: tab,
        selected: tab == bloc.currentTab,
        onSelect: () {
          bloc.request.add(FocusTabAction(tab: tab));
        },
        onClose: () {
          bloc.request.add(RemoveTabAction(tab: tab));
        },
      );
}

class _TabWidget extends StatefulWidget {
  const _TabWidget({this.bloc, this.selected, this.onSelect, this.onClose});
  final WebPageBloc bloc;
  final bool selected;
  final VoidCallback onSelect;
  final VoidCallback onClose;

  @override
  _TabWidgetState createState() => _TabWidgetState();
}

class _TabWidgetState extends State<_TabWidget> {
  final _hovering = ValueNotifier<bool>(false);
  @override
  Widget build(BuildContext context) {
    final baseTheme = Theme.of(context);
    return MouseRegion(
      onEnter: (_) {
        _hovering.value = true;
      },
      onExit: (_) {
        _hovering.value = false;
      },
      child: GestureDetector(
        onTap: widget.onSelect,
        child: Container(
          color:
              widget.selected ? baseTheme.accentColor : baseTheme.primaryColor,
          child: DefaultTextStyle(
            style: baseTheme.textTheme.body1.copyWith(
              color: widget.selected ? baseTheme.primaryColor : null,
            ),
            child: Stack(
              children: <Widget>[
                Center(
                  child: Padding(
                    padding: const EdgeInsets.symmetric(horizontal: 4.0),
                    child: AnimatedBuilder(
                      animation: widget.bloc.pageTitleNotifier,
                      builder: (_, __) => Text(
                        widget.bloc.pageTitle ?? 'NEW TAB',
                        maxLines: 1,
                        overflow: TextOverflow.ellipsis,
                      ),
                    ),
                  ),
                ),
                Positioned(
                  top: 0.0,
                  right: 0.0,
                  bottom: 0.0,
                  child: AnimatedBuilder(
                    animation: _hovering,
                    builder: (_, child) => Offstage(
                      offstage: !(widget.selected || _hovering.value),
                      child: child,
                    ),
                    child: AspectRatio(
                      aspectRatio: 1.0,
                      child: GestureDetector(
                        onTap: widget.onClose,
                        child: Container(
                          padding: const EdgeInsets.all(1.0),
                          alignment: Alignment.center,
                          child: Text('×'),
                        ),
                      ),
                    ),
                  ),
                ),
              ],
            ),
          ),
        ),
      ),
    );
  }
}
