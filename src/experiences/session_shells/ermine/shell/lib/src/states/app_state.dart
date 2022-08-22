// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:ermine/src/services/automator_service.dart';
import 'package:ermine/src/services/focus_service.dart';
import 'package:ermine/src/services/launch_service.dart';
import 'package:ermine/src/services/preferences_service.dart';
import 'package:ermine/src/services/presenter_service.dart';
import 'package:ermine/src/services/shortcuts_service.dart';
import 'package:ermine/src/services/startup_service.dart';
import 'package:ermine/src/services/user_feedback_service.dart';
import 'package:ermine/src/states/app_state_impl.dart';
import 'package:ermine/src/states/settings_state.dart';
import 'package:ermine/src/states/view_state.dart';
import 'package:ermine_utils/ermine_utils.dart';
import 'package:flutter/material.dart' hide Action, AppBar;
import 'package:fuchsia_scenic/views.dart';

enum FeedbackPage {
  preparing,
  scrim,
  ready,
  submitting,
  submitted,
  failed,
}

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
  bool get dialogsVisible;
  bool get appBarVisible;
  bool get sideBarVisible;
  bool get userFeedbackVisible;
  bool get overlaysVisible;
  bool get isIdle;
  bool get switcherVisible;
  bool get viewsVisible;
  bool get isUserFeedbackEnabled;
  ViewState get topView;
  ViewState? get switchTarget;
  List<DialogInfo> get dialogs;
  List<ViewState> get views;
  Map<String, List<String>> get errors;
  Locale? get locale;
  String get buildVersion;
  List<Map<String, String>> get appLaunchEntries;
  FeedbackPage get feedbackPage;
  String get feedbackUuid;
  String get feedbackErrorMsg;
  String get simpleLocale;
  double get scale;

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
  void launch(String title, String url, {String? alternateServiceName});
  void setTheme({bool darkTheme});
  void restart();
  void shutdown();
  void logout();
  void launchFeedback();
  void showUserFeedback();
  void closeUserFeedback();
  void launchLicense();
  void checkingForUpdatesAlert();
  void userFeedbackSubmit(
      {required String desc, required String username, String title});
  void setScale(double scale);
  void dismissDialogs();

  factory AppState.fromEnv() {
    return AppStateImpl(
      automatorService: AutomatorService(),
      launchService: LaunchService(),
      startupService: StartupService(),
      presenterService: PresenterService(),
      focusService: FocusService(ScenicContext.hostViewRef()),
      shortcutsService: ShortcutsService(ScenicContext.hostViewRef()),
      preferencesService: PreferencesService(),
      userFeedbackService: UserFeedbackService(),
    );
  }
}
