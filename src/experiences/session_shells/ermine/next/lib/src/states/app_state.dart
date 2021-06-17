// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:ui';

import 'package:flutter/material.dart' hide Action, AppBar;
import 'package:fuchsia_scenic/views.dart';
import 'package:mobx/mobx.dart';

import 'package:next/src/services/focus_service.dart';
import 'package:next/src/services/launch_service.dart';
import 'package:next/src/services/pointer_events_service.dart';
import 'package:next/src/services/preferences_service.dart';
import 'package:next/src/services/presenter_service.dart';
import 'package:next/src/services/shortcuts_service.dart';
import 'package:next/src/services/startup_service.dart';
import 'package:next/src/states/app_state_impl.dart';
import 'package:next/src/states/settings_state.dart';
import 'package:next/src/states/view_state.dart';
import 'package:next/src/widgets/app_bar.dart';
import 'package:next/src/widgets/side_bar.dart';

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
  ObservableValue<bool> get appBarVisible;
  ObservableValue<bool> get sideBarVisible;
  ObservableValue<bool> get overlaysVisible;
  ObservableValue<ViewState> get topView;
  ObservableList<ViewState> get views;
  ObservableStream<Locale> get localeStream;
  String get buildVersion;

  SettingsState get settingsState;

  Action get showOverlay;
  Action get hideOverlay;
  Action get showAppBar;
  Action get showSideBar;
  Action get switchNext;
  Action get switchPrev;
  Action get cancel;
  Action get closeView;
  Action get launch;
  Action get setTheme;
  Action get restart;
  Action get shutdown;

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
