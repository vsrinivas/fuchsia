// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:flutter/material.dart';
import 'package:internationalization/strings.dart';
import '../blocs/tabs_bloc.dart';
import '../blocs/webpage_bloc.dart';
import '../models/tabs_action.dart';

const _kTabBarHeight = 24.0;
const _kMinTabWidth = 120.0;
const _kSeparatorWidth = 1.0;
const _kTabPadding = EdgeInsets.symmetric(horizontal: _kTabBarHeight);
const _kScrollToMargin = _kMinTabWidth / 3;
const _kCloseMark = '×';

@visibleForTesting
double get kTabBarHeight => _kTabBarHeight;

@visibleForTesting
double get kMinTabWidth => _kMinTabWidth;

@visibleForTesting
double get kSeparatorWidth => _kSeparatorWidth;

@visibleForTesting
double get kScrollToMargin => _kScrollToMargin;

@visibleForTesting
String get kCloseMark => _kCloseMark;

class TabsWidget extends StatefulWidget {
  final TabsBloc bloc;
  const TabsWidget({@required this.bloc});

  @override
  _TabsWidgetState createState() => _TabsWidgetState();
}

class _TabsWidgetState extends State<TabsWidget> {
  final _scrollController = ScrollController();

  @override
  void initState() {
    super.initState();
    _setupBloc(null, widget);
  }

  @override
  void dispose() {
    _setupBloc(widget, null);
    _scrollController.dispose();
    super.dispose();
  }

  @override
  void didUpdateWidget(TabsWidget oldWidget) {
    super.didUpdateWidget(oldWidget);
    _setupBloc(oldWidget, widget);
  }

  void _setupBloc(TabsWidget oldWidget, TabsWidget newWidget) {
    if (oldWidget?.bloc != newWidget?.bloc) {
      oldWidget?.bloc?.currentTabNotifier?.removeListener(_onCurrentTabChanged);
      widget?.bloc?.currentTabNotifier?.addListener(_onCurrentTabChanged);
    }
  }

  void _onCurrentTabChanged() {
    if (_scrollController.hasClients) {
      final viewportWidth = _scrollController.position.viewportDimension;
      final currentTabIndex = widget.bloc.currentTabIdx;
      final currentTabPosition =
          currentTabIndex * (_kMinTabWidth + _kSeparatorWidth);

      final offsetForLeftEdge = currentTabPosition - _kScrollToMargin;
      final offsetForRightEdge =
          currentTabPosition - viewportWidth + _kMinTabWidth + _kScrollToMargin;

      double newOffset;

      if (_scrollController.offset > offsetForLeftEdge) {
        newOffset = offsetForLeftEdge;
      } else if (_scrollController.offset < offsetForRightEdge) {
        newOffset = offsetForRightEdge;
      }

      if (newOffset != null) {
        _scrollController.animateTo(
          newOffset,
          duration: Duration(milliseconds: 300),
          curve: Curves.ease,
        );
      }
    }
  }

  @override
  Widget build(BuildContext context) => AnimatedBuilder(
        animation: Listenable.merge(
            [widget.bloc.tabsNotifier, widget.bloc.currentTabNotifier]),
        builder: (_, __) => widget.bloc.tabs.length > 1
            ? Container(
                height: _kTabBarHeight,
                color: Theme.of(context).accentColor,
                padding: EdgeInsets.symmetric(vertical: 1.0),
                child: LayoutBuilder(
                  builder: (context, constraints) => widget.bloc.tabs.length <
                          constraints.maxWidth / _kMinTabWidth
                      ? Row(children: _buildPageTabs(width: null))
                      : ListView(
                          controller: _scrollController,
                          scrollDirection: Axis.horizontal,
                          children: _buildPageTabs(width: _kMinTabWidth),
                        ),
                ),
              )
            : Offstage(),
      );

  List<Widget> _buildPageTabs({@required double width}) => widget.bloc.tabs
      .map(_buildTab)
      // add a 1pip separator before every tab,
      // divide the rest of the space between tabs
      .expand((item) => [
            SizedBox(width: _kSeparatorWidth),
            width == null
                ? Expanded(child: item, flex: 1)
                : SizedBox(child: item, width: width),
          ])
      // skip the first separator
      .skip(1)
      .toList();

  Widget _buildTab(WebPageBloc tab) => _TabWidget(
        bloc: tab,
        selected: tab == widget.bloc.currentTab,
        onSelect: () => widget.bloc.request.add(FocusTabAction(tab: tab)),
        onClose: () => widget.bloc.request.add(RemoveTabAction(tab: tab)),
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
        WidgetsBinding.instance.addPostFrameCallback((_) {
          _hovering.value = false;
        });
      },
      child: GestureDetector(
        onTap: widget.onSelect,
        child: Container(
          color:
              widget.selected ? baseTheme.accentColor : baseTheme.primaryColor,
          child: DefaultTextStyle(
            style: TextStyle(
                color: widget.selected ? baseTheme.primaryColor : null),
            child: Stack(
              children: <Widget>[
                Center(
                  child: Padding(
                    padding: _kTabPadding,
                    child: AnimatedBuilder(
                      animation: widget.bloc.pageTitleNotifier,
                      builder: (_, __) => Text(
                        widget.bloc.pageTitle ?? Strings.newtab.toUpperCase(),
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
                          color: Colors.transparent,
                          alignment: Alignment.center,
                          child: Text(_kCloseMark),
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
