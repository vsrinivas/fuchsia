// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:async';
import 'dart:collection';
import 'package:meta/meta.dart';
import 'package:flutter/foundation.dart';
import '../blocs/webpage_bloc.dart';
import '../models/tabs_action.dart';

// Business logic for browser tabs.
// Sinks:
//   TabsAction: a tabs action - open new tab, focus tab, etc.
// Value Notifiers:
//   Tabs: the list of open tabs.
//   CurrentTab: the currently focused tab.
class TabsBloc {
  final _tabsList = <WebPageBloc>[];
  final WebPageBloc Function() tabFactory;
  final void Function(WebPageBloc tab) disposeTab;

  // Value Notifiers
  final ValueNotifier<UnmodifiableListView<WebPageBloc>> _tabs =
      ValueNotifier<UnmodifiableListView<WebPageBloc>>(
          UnmodifiableListView(<WebPageBloc>[]));
  final ValueNotifier<WebPageBloc> _currentTab =
      ValueNotifier<WebPageBloc>(null);

  ChangeNotifier get tabsNotifier => _tabs;
  ChangeNotifier get currentTabNotifier => _currentTab;

  UnmodifiableListView<WebPageBloc> get tabs => _tabs.value;
  WebPageBloc get currentTab => _currentTab.value;
  int get currentTabIdx => _tabsList.indexOf(currentTab);
  bool get isOnlyTab => _tabsList.length == 1;

  WebPageBloc get previousTab {
    int prevIdx = currentTabIdx - 1;
    prevIdx = (prevIdx < 0) ? (_tabsList.length - 1) : prevIdx;
    return _tabsList[prevIdx];
  }

  WebPageBloc get nextTab {
    int nextIdx = currentTabIdx + 1;
    nextIdx = (nextIdx > _tabsList.length - 1) ? 0 : nextIdx;
    return _tabsList[nextIdx];
  }

  // Sinks
  final _tabsActionController = StreamController<TabsAction>();
  Sink<TabsAction> get request => _tabsActionController.sink;

  TabsBloc({@required this.tabFactory, @required this.disposeTab}) {
    _tabsActionController.stream.listen(_onTabsActionChanged);
  }

  void dispose() {
    _tabsList.forEach(disposeTab);
    _tabsActionController.close();
  }

  void _onTabsActionChanged(TabsAction action) {
    switch (action.op) {
      case TabsActionType.newTab:
        final tab = tabFactory();
        _tabsList.add(tab);
        _tabs.value = UnmodifiableListView<WebPageBloc>(_tabsList);
        _currentTab.value = tab;
        break;
      case TabsActionType.focusTab:
        final FocusTabAction focusTab = action;
        _currentTab.value = focusTab.tab;
        break;
      case TabsActionType.removeTab:
        if (tabs.isEmpty) {
          break;
        }

        final RemoveTabAction removeTab = action;
        final tab = removeTab.tab;

        if (tabs.length == 1) {
          _currentTab.value = null;
        } else if (currentTab == removeTab.tab) {
          final indexOfRemoved = _tabsList.indexOf(tab);
          final indexOfNewTab =
              indexOfRemoved == 0 ? indexOfRemoved + 1 : indexOfRemoved - 1;
          _currentTab.value = _tabsList.elementAt(indexOfNewTab);
        }

        _tabsList.remove(tab);
        _tabs.value = UnmodifiableListView<WebPageBloc>(_tabsList);
        disposeTab(tab);
        break;
      case TabsActionType.addTab:
        final AddTabAction addTabAction = action;
        _tabsList.add(addTabAction.tab);
        _tabs.value = UnmodifiableListView<WebPageBloc>(_tabsList);
        _currentTab.value = addTabAction.tab;
        break;
    }
  }
}
