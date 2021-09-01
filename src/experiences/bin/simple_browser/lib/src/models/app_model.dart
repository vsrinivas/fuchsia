// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
import 'dart:async';
import 'package:fidl_fuchsia_intl/fidl_async.dart';
import 'package:flutter/material.dart';
import 'package:fuchsia_internationalization_flutter/internationalization.dart';
import 'package:fuchsia_scenic/views.dart';
import 'package:fuchsia_services/services.dart';
import 'package:keyboard_shortcuts/keyboard_shortcuts.dart';
import '../blocs/tabs_bloc.dart';
import '../models/tabs_action.dart';
import '../models/webpage_action.dart';
import '../utils/browser_shortcuts.dart';

/// Model that handles the browser's tab and webpage states.
///
/// tabsBloc: A listener for the tab and web page states.
/// localeStream: A getter for the current localization value.
/// initialLocale: A getter for the initial localization value.
/// keyboardShortcuts: A getter for the browser's [KeyboardShortcuts].
class AppModel {
  final TabsBloc tabsBloc;
  Stream<Locale> _localeStream;
  late final FocusNode fieldFocus;
  late final KeyboardShortcuts? _keyboardShortcuts;

  AppModel({
    required this.tabsBloc,
    required Stream<Locale> localeStream,
  }) : _localeStream = localeStream {
    fieldFocus = FocusNode();
    _keyboardShortcuts =
        BrowserShortcuts(tabsBloc: tabsBloc, actions: _shortcutActions())
            .activateShortcuts(ScenicContext.hostViewRef());
  }

  factory AppModel.fromStartupContext({required TabsBloc tabsBloc}) {
    final _intl = PropertyProviderProxy();
    Incoming.fromSvcPath()
      ..connectToService(_intl)
      ..close();
    final localStream = LocaleSource(_intl).stream().asBroadcastStream();

    return AppModel(
      tabsBloc: tabsBloc,
      localeStream: localStream,
    );
  }

  Stream<Locale> get localeStream => _localeStream;

  KeyboardShortcuts? get keyboardShortcuts => _keyboardShortcuts;

  void newTab() => tabsBloc.request.add(NewTabAction());

  Map<String, VoidCallback> _shortcutActions() {
    return {
      'newTab': newTab,
      'closeTab': _closeTab,
      'goBack': _goBack,
      'goForward': _goForward,
      'refresh': _refresh,
      'previousTab': _previousTab,
      'nextTab': _nextTab,
      'focusField': _focusField,
    };
  }

  void _closeTab() {
    if (tabsBloc.isOnlyTab) {
      return;
    }
    tabsBloc.request.add(RemoveTabAction(tab: tabsBloc.currentTab!));
  }

  void _goBack() {
    if (tabsBloc.currentTab!.backState) {
      tabsBloc.currentTab!.request.add(GoBackAction());
      return;
    }
    return;
  }

  void _goForward() {
    if (tabsBloc.currentTab!.forwardState) {
      tabsBloc.currentTab!.request.add(GoForwardAction());
      return;
    }
    return;
  }

  void _refresh() => tabsBloc.currentTab!.request.add(RefreshAction());

  void _previousTab() {
    if (tabsBloc.isOnlyTab || tabsBloc.previousTab == null) {
      return;
    }
    tabsBloc.request.add(FocusTabAction(tab: tabsBloc.previousTab!));
  }

  void _nextTab() {
    if (tabsBloc.isOnlyTab || tabsBloc.nextTab == null) {
      return;
    }
    tabsBloc.request.add(FocusTabAction(tab: tabsBloc.nextTab!));
  }

  void _focusField() => fieldFocus.requestFocus();

  // ignore: avoid_positional_boolean_parameters
  void onFocus(bool focused) {
    if (focused && tabsBloc.currentTab!.webService.isLoaded) {
      tabsBloc.currentTab!.request.add(SetFocusAction());
    }
  }

  // TODO: Activate/Deactivate the keyboardShortcuts depending on the browser's
  // focus state when its relevant support is ready (fxb/42185)
}
