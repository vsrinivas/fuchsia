// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
import 'dart:async';
import 'package:fidl_fuchsia_intl/fidl_async.dart';
import 'package:flutter/material.dart';
import 'package:fuchsia_internationalization_flutter/internationalization.dart';
import 'package:fuchsia_services/services.dart';
import 'package:keyboard_shortcuts/keyboard_shortcuts.dart';
import 'package:simple_browser/src/blocs/webpage_bloc.dart';
import '../blocs/tabs_bloc.dart';
import '../models/tabs_action.dart';

/// Model that handles the browser's tab and webpage states.
///
/// tabsBloc: A listener for the tab and web page states.
/// localeStream: A getter for the current localization value.
/// initialLocale: A getter for the initial localization value.
/// keyboardShortcuts: A getter for the browser's [KeyboardShortcuts].
class AppModel {
  final TabsBloc<WebPageBloc> tabsBloc;
  Stream<Locale> _localeStream;
  Locale _initialLocale;
  final KeyboardShortcuts _keyboardShortcuts;

  AppModel({
    @required this.tabsBloc,
    @required Stream<Locale> localeStream,
    KeyboardShortcuts keyboardShortcuts,
  })  : _localeStream = localeStream,
        _keyboardShortcuts = keyboardShortcuts;

  factory AppModel.fromStartupContext({
    @required TabsBloc<WebPageBloc> tabsBloc,
    KeyboardShortcuts keyboardShortcuts,
  }) {
    final _intl = PropertyProviderProxy();
    StartupContext.fromStartupInfo().incoming.connectToService(_intl);
    final localStream = LocaleSource(_intl).stream();

    return AppModel(
      tabsBloc: tabsBloc,
      keyboardShortcuts: keyboardShortcuts,
      localeStream: localStream,
    );
  }

  Future<void> init() async {
    _initialLocale = await _localeStream.asBroadcastStream().first;
  }

  Stream<Locale> get localeStream => _localeStream;
  Locale get initialLocale => _initialLocale;
  KeyboardShortcuts get keyboardShortcuts => _keyboardShortcuts;

  void newTab() => tabsBloc.request.add(NewTabAction<WebPageBloc>());

  // TODO: Activate/Deactivate the keyboardShortcuts depending on the browser's
  // focus state when its relevant support is ready (fxb/42185)
}
