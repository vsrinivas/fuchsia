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

/// Defines the state of the entire application.
///
/// The state of the application is an encapsulation of:
/// - Observable state data.
/// - Read-only state data.
/// - Child states reachable from this state.
/// - Actions that can be invoked on this state.
abstract class AppState {
  ThemeData get theme;
  bool get hasDarkTheme;
  bool get alertsVisible;
  bool get appBarVisible;
  bool get sideBarVisible;
  bool get overlaysVisible;
  bool get oobeVisible;
  bool get isIdle;
  bool get switcherVisible;
  bool get viewsVisible;
  ViewState get topView;
  ViewState? get switchTarget;
  List<AlertInfo> get alerts;
  List<ViewState> get views;
  Map<String, List<String>> get errors;
  Locale? get locale;
  String get buildVersion;
  List<Map<String, String>> get appLaunchEntries;

  SettingsState get settingsState;

  void showOverlay();
  void hideOverlay();
  void showAppBar();
  void showSideBar();
  void switchNext();
  void switchPrev();
  void switchView(ViewState view);
  void cancel();
  void closeView();
  void launch(String title, String url);
  void setTheme({bool darkTheme});
  void restart();
  void shutdown();
  void launchFeedback();
  void launchLicense();
  void oobeFinished();
  void updateChannelAlert();

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
