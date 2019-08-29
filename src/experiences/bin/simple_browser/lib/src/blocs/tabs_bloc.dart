// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:async';
import 'dart:collection';
import 'package:meta/meta.dart';
import 'package:flutter/foundation.dart';
import '../models/tabs_action.dart';

// Business logic for browser tabs.
// Sinks:
//   TabsAction: a tabs action - open new tab, focus tab, etc.
// Value Notifiers:
//   Tabs: the list of open tabs.
//   CurrentTab: the currently focused tab.
class TabsBloc<T> {
  final _tabsList = <T>[];
  final T Function() tabFactory;
  final void Function(T tab) disposeTab;

  // Value Notifiers
  final ValueNotifier<UnmodifiableListView<T>> tabs =
      ValueNotifier<UnmodifiableListView<T>>(UnmodifiableListView(<T>[]));
  final ValueNotifier<T> currentTab = ValueNotifier<T>(null);

  // Sinks
  final _tabsActionController = StreamController<TabsAction<T>>();
  Sink<TabsAction<T>> get request => _tabsActionController.sink;

  TabsBloc({@required this.tabFactory, @required this.disposeTab}) {
    _tabsActionController.stream.listen(_handleAction);
  }

  void dispose() {
    _tabsList.forEach(disposeTab);
    _tabsActionController.close();
  }

  void _handleAction(TabsAction<T> action) {
    switch (action.op) {
      case TabsActionType.newTab:
        final tab = tabFactory();
        _tabsList.add(tab);
        tabs.value = UnmodifiableListView<T>(_tabsList);
        currentTab.value = tab;
        break;
      case TabsActionType.focusTab:
        final FocusTabAction<T> focusTab = action;
        currentTab.value = focusTab.tab;
        break;
    }
  }
}
