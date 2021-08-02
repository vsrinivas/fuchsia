// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:ui';

import 'package:ermine/src/services/focus_service.dart';
import 'package:ermine/src/services/launch_service.dart';
import 'package:ermine/src/services/pointer_events_service.dart';
import 'package:ermine/src/services/preferences_service.dart';
import 'package:ermine/src/services/presenter_service.dart';
import 'package:ermine/src/services/shortcuts_service.dart';
import 'package:ermine/src/services/startup_service.dart';
import 'package:ermine/src/states/app_state_impl.dart';
import 'package:ermine/src/states/settings_state.dart';
import 'package:ermine/src/states/view_state.dart';
import 'package:ermine/src/widgets/app_bar.dart';
import 'package:ermine/src/widgets/side_bar.dart';
import 'package:flutter/material.dart' hide Action, AppBar;
import 'package:fuchsia_scenic/views.dart';
import 'package:mobx/mobx.dart';

/// Defines the state of the entire application.
///
/// The state of the application is an encapsulation of:
/// - Observable state data.
/// - Read-only state data.
/// - Child states reachable from this state.
/// - Actions that can be invoked on this state.
abstract class AppState with Store {
  ObservableValue<ThemeData> get theme;
  ObservableValue<bool> get hasDarkTheme;
  ObservableValue<bool> get alertsVisible;
  ObservableValue<bool> get appBarVisible;
  ObservableValue<bool> get sideBarVisible;
  ObservableValue<bool> get overlaysVisible;
  ObservableValue<bool> get oobeVisible;
  ObservableValue<bool> get isIdle;
  ObservableValue<bool> get switcherVisible;
  ObservableValue<bool> get viewsVisible;
  ObservableValue<ViewState> get topView;
  ObservableValue<ViewState?> get switchTarget;
  ObservableList<AlertInfo> get alerts;
  ObservableList<ViewState> get views;
  ObservableMap<String, List<String>> get errors;
  ObservableStream<Locale> get localeStream;
  String get buildVersion;
  List<Map<String, String>> get appLaunchEntries;

  SettingsState get settingsState;

  Action get showOverlay;
  Action get hideOverlay;
  Action get showAppBar;
  Action get showSideBar;
  Action get switchNext;
  Action get switchPrev;
  Action get switchView;
  Action get cancel;
  Action get closeView;
  Action get launch;
  Action get setTheme;
  Action get restart;
  Action get shutdown;
  Action get launchFeedback;
  Action get launchLicense;
  Action get oobeFinished;

  factory AppState.fromEnv() {
    return AppStateImpl(
      launchService: LaunchService(),
      startupService: StartupService(),
      presenterService: PresenterService(),
      focusService: FocusService(ScenicContext.hostViewRef()),
      shortcutsService: ShortcutsService(ScenicContext.hostViewRef()),
      preferencesService: PreferencesService(),
      pointerEventsService: PointerEventsService(
        ScenicContext.hostViewRef(),
        insets: EdgeInsets.only(left: AppBar.kWidth, right: SideBar.kWidth),
      ),
    );
  }
}

class AlertInfo {
  String? title;
  String? content;
  Map<String, VoidCallback> buttons;

  AlertInfo({required this.buttons, this.title, this.content})
      : assert(title != null || content != null),
        assert(buttons.isNotEmpty);
}
