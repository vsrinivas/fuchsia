// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// ignore_for_file: unnecessary_lambdas
import 'dart:async';
import 'dart:convert';
import 'dart:io';

import 'package:collection/collection.dart';
import 'package:ermine/src/services/automator_service.dart';
import 'package:ermine/src/services/focus_service.dart';
import 'package:ermine/src/services/launch_service.dart';
import 'package:ermine/src/services/preferences_service.dart';
import 'package:ermine/src/services/presenter_service.dart';
import 'package:ermine/src/services/shortcuts_service.dart';
import 'package:ermine/src/services/startup_service.dart';
import 'package:ermine/src/services/user_feedback_service.dart';
import 'package:ermine/src/states/app_state.dart';
import 'package:ermine/src/states/settings_state.dart';
import 'package:ermine/src/states/view_state.dart';
import 'package:ermine/src/states/view_state_impl.dart';
import 'package:ermine_utils/ermine_utils.dart';
import 'package:fidl_ermine_tools/fidl_async.dart';
import 'package:fidl/fidl.dart';
import 'package:flutter/material.dart' hide Action;
import 'package:fuchsia_inspect/inspect.dart';
import 'package:fuchsia_logger/logger.dart';
import 'package:fuchsia_scenic_flutter/fuchsia_view.dart';
import 'package:internationalization/strings.dart';
import 'package:mobx/mobx.dart';

/// Defines the implementation of [AppState].
class AppStateImpl with Disposable implements AppState {
  final AutomatorService automatorService;
  final FocusService focusService;
  final LaunchService launchService;
  final StartupService startupService;
  final PresenterService presenterService;
  final ShortcutsService shortcutsService;
  final PreferencesService preferencesService;
  final UserFeedbackService userFeedbackService;

  static const kFeedbackUrl =
      'https://fuchsia.dev/fuchsia-src/contribute/report-issue';
  static const kChromeElementManager = 'fuchsia.element.Manager-chrome';
  static const kLicenseUrl =
      'fuchsia-pkg://fuchsia.com/license_settings#meta/license_settings.cm';
  static const kScreenSaverUrl =
      'fuchsia-pkg://fuchsia.com/screensaver#meta/screensaver.cm';
  static const kEnableUserFeedbackMarkerFile =
      '/pkg/config/enable_user_feedback';

  AppStateImpl({
    required this.automatorService,
    required this.startupService,
    required this.launchService,
    required this.focusService,
    required this.presenterService,
    required this.shortcutsService,
    required this.preferencesService,
    required this.userFeedbackService,
  }) : _localeStream = startupService.stream.asObservable() {
    launchService.onControllerClosed = _onElementClosed;
    focusService.onFocusMoved = _onFocusMoved;
    automatorService
      ..automator = _AutomatorImpl(this)
      ..serve(startupService.componentContext);
    presenterService
      ..onPresenterDisposed = dispose
      ..onViewPresented = _onViewPresented
      ..onViewDismissed = _onViewDismissed
      ..onError = _onPresentError
      ..advertise(startupService.componentContext.outgoing);

    // Register keyboard shortcuts and then initialize SettingsState with it.
    shortcutsService.register(_actions);
    settingsState = SettingsState.from(
        shortcutBindings: shortcutsService.keyboardBindings,
        displayDialog: _displayDialog);

    startupService
      ..onInspect = _onInspect
      ..onIdle = _onIdle
      ..onAltReleased = _triggerSwitch
      ..onPowerBtnPressed = _onPowerBtnPressed
      ..serve();
    userFeedbackService
      ..onSubmit = _onFeedbackSubmit
      ..onError = _onFeedbackError;

    // Add reactions to state changes.
    reactions
      ..add(reaction<bool>((_) => views.isNotEmpty, (hasViews) {
        // Display overlays when no views are present.
        if (!hasViews) {
          overlayVisibility.value = true;
        }
      }))
      ..add(reaction<bool>((_) => isIdle, (idle) async {
        if (idle) {
          // Start screenSaver.
          await launchService.launch('Screen Saver', kScreenSaverUrl);
        }
      }))
      ..add(reaction<Locale?>((_) => locale, (locale) {
        // Removes the U extensions and T extensions
        // http://www.unicode.org/reports/tr35/#36-unicode-bcp-47-u-extension
        // https://www.unicode.org/reports/tr35/tr35.html#BCP47_T_Extension
        _simpleLocale.value =
            locale?.toString().split('-u-').first.split('-t-').first ?? '';
      }));
  }

  @override
  void dispose() {
    super.dispose();

    settingsState.dispose();

    startupService.dispose();
    focusService.dispose();
    presenterService.dispose();
    shortcutsService.dispose();
    preferencesService.dispose();
    settingsState.dispose();
  }

  @override
  late final SettingsState settingsState;

  @override
  bool get isIdle => _isIdle.value;
  set isIdle(bool idle) => _isIdle.value = idle;
  final Observable<bool> _isIdle = false.asObservable();

  @override
  ThemeData get theme => _theme.value;
  late final _theme = (() {
    return preferencesService.darkMode.value
        ? AppTheme.darkTheme
        : AppTheme.lightTheme;
  }).asComputed();

  @override
  bool get hasDarkTheme => preferencesService.darkMode.value;

  /// Defines the visible status of overlays visible in 'overview' state. At the
  /// moment this is the [AppBar] and the [SideBar].
  final overlayVisibility = true.asObservable();

  /// A flag that is set to true when the [AppBar] should be peeking.
  final appBarPeeking = false.asObservable();

  /// A flag that is set to true when the [SideBar] should be peeking.
  final sideBarPeeking = false.asObservable();

  /// A flag that is set to true when an app is launched from the app launcher.
  final appIsLaunching = false.asObservable();

  /// A flag that is set to true when the [UserFeedback] is triggered.
  final userFeedbackVisibility = false.asObservable();

  @override
  bool get dialogsVisible => _dialogsVisible.value;
  late final _dialogsVisible = (() {
    return dialogs.isNotEmpty;
  }).asComputed();

  /// Returns true if shell has focus and any side bars are visible.
  @override
  bool get overlaysVisible => _overlaysVisible.value;
  late final _overlaysVisible = (() {
    return !isIdle &&
        !appIsLaunching.value &&
        shellHasFocus.value &&
        (appBarVisible ||
            sideBarVisible ||
            switcherVisible ||
            dialogsVisible ||
            userFeedbackVisible);
  }).asComputed();

  @override
  bool get appBarVisible => _appBarVisible.value;
  late final _appBarVisible = (() {
    return shellHasFocus.value &&
        views.isNotEmpty &&
        topView.loading &&
        (appBarPeeking.value || overlayVisibility.value);
  }).asComputed();

  @override
  bool get sideBarVisible => _sideBarVisible.value;
  late final _sideBarVisible = (() {
    return shellHasFocus.value &&
        (sideBarPeeking.value || overlayVisibility.value);
  }).asComputed();

  @override
  bool get userFeedbackVisible => _userFeedbackVisible.value;
  late final _userFeedbackVisible = (() {
    return shellHasFocus.value && userFeedbackVisibility.value;
  }).asComputed();

  @override
  final dialogs = <DialogInfo>[].asObservable();

  @override
  final errors = <String, List<String>>{}.asObservable();

  @override
  final views = <ViewState>[].asObservable();

  @override
  bool get switcherVisible => _switcherVisible.value;
  set switcherVisible(bool visible) => _switcherVisible.value = visible;
  final Observable<bool> _switcherVisible = false.asObservable();

  @override
  FeedbackPage get feedbackPage => _feedbackPage.value;
  set feedbackPage(FeedbackPage value) => _feedbackPage.value = value;
  final _feedbackPage = Observable<FeedbackPage>(FeedbackPage.preparing);

  @override
  String get feedbackUuid => _feedbackUuid.value;
  set feedbackUuid(String value) => _feedbackUuid.value = value;
  final _feedbackUuid = ''.asObservable();

  @override
  String get feedbackErrorMsg => _feedbackErrorMsg.value;
  set feedbackErrorMsg(String value) => _feedbackErrorMsg.value = value;
  final _feedbackErrorMsg = ''.asObservable();

  @override
  bool get viewsVisible => _viewsVisible.value;
  late final _viewsVisible = () {
    return views.isNotEmpty && !isIdle;
  }.asComputed();

  @override
  Locale? get locale => _localeStream.value;
  final ObservableStream<Locale> _localeStream;

  @override
  String get simpleLocale => _simpleLocale.value;
  final _simpleLocale = ''.asObservable();

  @override
  double get scale => preferencesService.scale;

  late final shellHasFocus = (() {
    return startupService.hostView == _focusedView.value;
  }).asComputed();

  @override
  ViewState get topView => _topView.value;
  set topView(ViewState view) => _topView.value = view;
  late final Observable<ViewState> _topView =
      Observable<ViewState>(views.first);

  @override
  ViewState? get switchTarget => _switchTarget.value;
  set switchTarget(ViewState? view) => _switchTarget.value = view;
  final Observable<ViewState?> _switchTarget = Observable<ViewState?>(null);

  @override
  String get buildVersion => startupService.buildVersion;

  @override
  List<Map<String, String>> get appLaunchEntries =>
      startupService.appLaunchEntries;

  @override
  bool get isUserFeedbackEnabled => _isUserFeedbackEnabled.value;
  late final _isUserFeedbackEnabled = Observable<bool>(() {
    File config = File(kEnableUserFeedbackMarkerFile);
    if (!config.existsSync()) {
      log.info('User feedback disabled.');
      return false;
    }
    log.info('User feedback enabled.');
    return true;
  }());

  void setFocusToShellView() {
    focusService.setFocusOnHostView();
  }

  void setFocusToChildView() {
    if (views.isNotEmpty) {
      focusService.setFocusOnView(topView);
    }
  }

  @override
  void showOverlay() => _showOverlay();
  late final _showOverlay = () {
    appIsLaunching.value = false;
    overlayVisibility.value = true;
    setFocusToShellView();
  }.asAction();

  @override
  void hideOverlay() => _hideOverlay();
  late final Action _hideOverlay = setFocusToChildView.asAction();

  @override
  void showAppBar() => showOverlay();

  @override
  void showSideBar() => showOverlay();

  @override
  void switchView(ViewState view) => _switchView([view]);
  late final _switchView = (ViewState view) {
    topView = view;
    setFocusToChildView();
  }.asAction();

  @override
  void switchNext() => _switchNext();
  late final _switchNext = () {
    if (views.length > 1) {
      // Initialize [switchTarget] with the top view, if not already set.
      switchTarget ??= topView;

      // Get next view from top view. Wrap to first view in list, if it is last.
      switchTarget = switchTarget == views.last
          ? views.first
          : views[views.indexOf(switchTarget!) + 1];

      // Set focus to shell view so that we can receive the final Alt key press.
      setFocusToShellView();

      // Display the app switcher.
      switcherVisible = true;
    }
  }.asAction();

  @override
  void switchPrev() => _switchPrev();
  late final _switchPrev = () {
    if (views.length > 1) {
      // Initialize [switchTarget] with the top view, if not already set.
      switchTarget ??= topView;

      switchTarget = switchTarget == views.first
          ? views.last
          : views[views.indexOf(switchTarget!) - 1];

      // Set focus to shell view so that we can receive the final Alt key press.
      setFocusToShellView();

      // Display the app switcher.
      switcherVisible = true;
    }
  }.asAction();

  void _triggerSwitch() {
    if (switchTarget != null) {
      runInAction(() {
        if (switchTarget != topView) {
          topView = switchTarget!;
          setFocusToChildView();
        }

        switcherVisible = false;
        switchTarget = null;
      });
    }
  }

  @override
  void cancel() {
    if (!dialogsVisible) {
      hideOverlay();
    }
    // If top view is a screensaver, dismiss it.
    // TODO(fxb/80131): Use cancel action associated with Esc keyboard shortcut
    // to dismiss the screensaver, since mouse and keyboard input is not
    // available to the shell when a child view is fullscreen.
    if (views.isNotEmpty && topView.url == kScreenSaverUrl) {
      _onIdle(idle: false);
    }
  }

  @override
  void closeView() => _closeView();
  late final Action _closeView = () {
    if (views.isEmpty) {
      return;
    }
    appIsLaunching.value = false;
    topView.close();
  }.asAction();

  late final Action closeAll = () {
    for (final view in views) {
      view.close();
    }
    views.clear();
  }.asAction();

  @override
  void launch(String title, String url, {String? alternateServiceName}) =>
      _launch([title, url, alternateServiceName]);
  late final _launch =
      (String title, String url, String? alternateServiceName) async {
    try {
      _clearError(url, 'ProposeElementError');

      log.info(
          'Launching $title [$url] using ${alternateServiceName ?? 'ElementManager'}');

      // For web urls use Chrome's element manager service.
      if (url.startsWith('http')) {
        alternateServiceName ??= kChromeElementManager;
      }
      await launchService.launch(title, url,
          alternateServiceName: alternateServiceName);
      // Hide app launcher unless we had an error presenting the view.
      if (!_isLaunchError(url)) {
        runInAction(() {
          appIsLaunching.value = true;
        });
      }
      // ignore: avoid_catches_without_on_clauses
    } catch (e) {
      _onLaunchError(url, e.toString());
      log.shout('$e: Failed to propose element <$url>');
    }
  }.asAction();

  @override
  void launchFeedback() => launch(Strings.feedback, kFeedbackUrl);

  /// A flag to remember the previous overlays visibility status before showing
  /// "Report an Issue" screen to set the overlays status back as it were.
  ///
  /// Usecase 1: The user opens "Report an Issue" using the keyboard shortcut
  /// while using a full-screen chromium app -> "Report an Issue"(overlay) comes
  /// up on the top -> The user closes user feedback -> The full-screen chromium
  /// comes back to the top.
  ///
  /// Usecase 2: The user opens overlays(sidebar, app bar) while using a full-
  /// screen chromium app -> The user clicks "Report an Issue" on Quick Settings
  /// -> The user closes "Report an Issue" -> The app bar and side bar still
  /// remain on top of the chromium view.
  bool _wasOverlayVisible = true;

  @override
  void showUserFeedback() async {
    if (!settingsState.dataSharingConsentEnabled) {
      _displayDialog(AlertDialogInfo(
        title: Strings.turnOnDataSharingTitle,
        body: Strings.turnOnDataSharingBody,
        actions: [Strings.close],
        width: 714,
      ));
      return;
    }

    runInAction(() {
      // TODO(fxb/97464): Take a screenshot here when the bug is fixed.
      _wasOverlayVisible = overlaysVisible;
      userFeedbackVisibility.value = true;
      showOverlay();

      if (preferencesService.showUserFeedbackStartUpDialog.value) {
        _feedbackPage.value = FeedbackPage.scrim;
        _displayDialog(CheckboxDialogInfo(
          body: '${Strings.firstTimeUserFeedback1}\n\n'
              '${Strings.firstTimeUserFeedback2('go/workstation-feedback')}',
          checkboxLabel: Strings.doNotShowAgain,
          onSubmit: (value) {
            runInAction(() {
              if (value == true) {
                preferencesService.showUserFeedbackStartUpDialog.value = false;
                log.info(
                    'Set to not show the user feedback startup message again.');
              }
              _feedbackPage.value = FeedbackPage.ready;
            });
          },
          actions: [Strings.okGotIt],
          defaultAction: Strings.okGotIt,
          width: 790,
        ));

        return;
      }

      _feedbackPage.value = FeedbackPage.ready;
    });
  }

  @override
  void closeUserFeedback() {
    runInAction(() {
      userFeedbackVisibility.value = false;
      if (!_wasOverlayVisible) {
        hideOverlay();
      }
      _feedbackPage.value = FeedbackPage.preparing;
      _feedbackUuid.value = '';
    });
  }

  @override
  void userFeedbackSubmit(
      {required String desc,
      required String username,
      String title = 'New user feedback for Workstation'}) {
    userFeedbackService.submit(title, desc, username);
  }

  @override
  void setScale(double scale) => preferencesService.scale = scale;

  @override
  void launchLicense() => launch(Strings.license, kLicenseUrl);

  @override
  void setTheme({bool darkTheme = true}) => _setTheme([darkTheme]);
  late final Action _setTheme = (bool darkTheme) {
    preferencesService.darkMode.value = darkTheme;
  }.asAction();

  @override
  void restart() {
    _displayDialog(AlertDialogInfo(
      title: Strings.confirmRestartAlertTitle,
      body: Strings.confirmToSaveWorkAlertBody,
      actions: [Strings.cancel, Strings.restart],
      defaultAction: Strings.restart,
      onAction: (action) {
        if (action == Strings.restart) {
          startupService.restartDevice();
          // Clean up.
          dispose();
        }
      },
    ));
  }

  @override
  void shutdown() {
    _displayDialog(AlertDialogInfo(
      title: Strings.confirmShutdownAlertTitle,
      body: Strings.confirmToSaveWorkAlertBody,
      actions: [Strings.cancel, Strings.shutdown],
      defaultAction: Strings.shutdown,
      onAction: (action) {
        if (action == Strings.shutdown) {
          startupService.shutdownDevice();
          // Clean up.
          dispose();
        }
      },
    ));
  }

  @override
  void logout() {
    _displayDialog(AlertDialogInfo(
      title: Strings.confirmLogoutAlertTitle,
      body: Strings.confirmToSaveWorkAlertBody,
      actions: [Strings.cancel, Strings.logout],
      defaultAction: Strings.logout,
      onAction: (action) {
        if (action == Strings.logout) {
          dispose();
          startupService.logout();
        }
      },
    ));
  }

  @override
  void checkingForUpdatesAlert() {
    _displayDialog(AlertDialogInfo(
      title: Strings.channelUpdateAlertTitle,
      body: Strings.channelUpdateAlertBody,
      actions: [Strings.close, Strings.continueLabel],
      onAction: (action) {
        if (action == Strings.continueLabel) {
          settingsState.checkForUpdates();
        }
      },
    ));
  }

  @override
  void dismissDialogs() {
    if (appBarVisible || sideBarVisible || userFeedbackVisible) {
      return;
    }
    hideOverlay();
  }

  late final showScreenSaver = () {
    _onIdle(idle: true);
  }.asAction();

  // Map key shortcuts to corresponding actions.
  Map<String, dynamic> get _actions {
    final actions = {
      'launcher': showOverlay,
      'switchNext': switchNext,
      'switchPrev': switchPrev,
      'cancel': cancel,
      'close': closeView,
      'closeAll': closeAll,
      'settings': showOverlay,
      'shortcuts': () {
        settingsState.showShortcutSettings();
        showOverlay();
      },
      'screenSaver': showScreenSaver,
      'inspect': () => json.encode(_getInspectData()),
      'navigateBack': () {
        if (!settingsState.allSettingsPageVisible) {
          settingsState.showAllSettings();
        }
      },
      'increaseBrightness': () => settingsState.increaseBrightness(),
      'decreaseBrightness': () => settingsState.decreaseBrightness(),
      'increaseVolume': () => settingsState.increaseVolume(),
      'decreaseVolume': () => settingsState.decreaseVolume(),
      'muteVolume': () => settingsState.toggleMute(),
      'logout': logout,
      'zoomIn': preferencesService.zoomIn,
      'zoomOut': preferencesService.zoomOut,
    };

    if (isUserFeedbackEnabled) {
      actions.addAll({'reportAnIssue': showUserFeedback});
    }

    return actions;
  }

  final _focusedView = Observable<ViewHandle?>(null);
  void _onFocusMoved(ViewHandle viewHandle) {
    if (_focusedView.value == viewHandle) {
      return;
    }

    runInAction(() {
      appIsLaunching.value = false;
      _focusedView.value = viewHandle;

      if (viewHandle == startupService.hostView) {
        // Start SettingsState refresh.
        settingsState.start();
      } else {
        overlayVisibility.value = false;

        // Stop SettingsState refresh.
        settingsState.stop();

        // If an app view has focus, bring it to the top.
        for (final view in views) {
          if (viewHandle == view.view) {
            topView = view;
            break;
          }
        }
      }
    });
  }

  bool _onViewPresented(ViewState viewState) {
    final view = viewState as ViewStateImpl;

    // TODO(https://fxbug.dev/82840): Remove this block once this issue is
    // fixed. Since the current top view looses hittesting functionality,
    // explicitly reset the hittest flag on current topView before dismissing
    // the overlays.
    if (views.isNotEmpty) {
      FuchsiaViewsService.instance
          .updateView(topView.viewConnection.viewId)
          .catchError((e) {
        log.warning('Error calling updateView on ${topView.title}: $e');
      });
    }

    runInAction(() {
      // Make this view the top view.
      views.add(view);
      topView = view;

      // If any, remove previously cached launch errors for the app.
      if (viewState.url != null) {
        _clearError(viewState.url!, 'ViewControllerEpitaph');
      }
    });

    // Focus on view when it is loading.
    view.reactions.add(reaction<bool>((_) => view.loading, (loading) {
      if (loading && view == topView && view.focusable) {
        setFocusToChildView();
      }
    }));

    // Update view hittestability based on overlay visibility.
    view.reactions.add(reaction<bool>(
        (_) => overlaysVisible || switcherVisible || dialogsVisible, (overlay) {
      // Don't reset hittest flag when showing app switcher, because the
      // app switcher does not react to pointer events.
      view.hitTestable = !overlay;
    }));

    // Remove view from views when it is closed.
    view.reactions.add(when((_) => view.closed, () {
      _onViewDismissed(view);
    }));
    return true;
  }

  // Called when a view is dismissed from an external source (not user).
  void _onViewDismissed(ViewState viewState) {
    runInAction(() {
      final view = viewState as ViewStateImpl;
      // Switch to previous view before closing this view if it was the top view
      // and there are other views.
      if (view == topView && views.length > 1) {
        final prevView = view != views.first
            ? views[views.indexOf(topView) - 1]
            : views.last;
        topView = prevView;
        setFocusToChildView();
      }

      views.remove(view);
      view.dispose();
    });
  }

  void _onPresentError(String url, String error) {
    runInAction(() {
      final errorSpec = error.split('.').last;
      final description = errorSpec == 'invalidViewSpec'
          ? Strings.invalidViewSpecDesc
          : errorSpec == 'rejected'
              ? Strings.viewPresentRejectedDesc
              : Strings.defaultPresentErrorDesc;
      const referenceLink =
          'https://fuchsia.dev/reference/fidl/fuchsia.element#GraphicalPresenter.PresentView';

      if (_isPrelistedApp(url)) {
        errors[url] = [description, '$error\n$referenceLink'];
      } else {
        _displayDialog(AlertDialogInfo(
          title: description,
          body: '${Strings.errorWhilePresenting},\n$url\n\n'
              '${Strings.errorType}: $error\n\n'
              '${Strings.moreErrorInformation}\n$referenceLink',
          actions: [Strings.close],
        ));
      }
    });
  }

  void _onLaunchError(String url, String error) {
    final proposeError = error.split(' ').last;
    print('Handling launch error $proposeError for $url');
    final errorSpec = proposeError.split('.').last;
    final description = errorSpec == 'notFound'
        ? Strings.urlNotFoundDesc
        : errorSpec == 'rejected'
            ? Strings.launchRejectedDesc
            : Strings.defaultProposeErrorDesc;
    const referenceLink =
        'https://fuchsia.dev/reference/fidl/fuchsia.element#Manager.ProposeElement';

    errors[url] = [
      description,
      '$proposeError\n\n${Strings.moreErrorInformation}\n$referenceLink'
    ];
  }

  void _onPowerBtnPressed() {
    _displayDialog(AlertDialogInfo(
      title: Strings.restartOrShutDown,
      body: Strings.powerBtnPressedDesc,
      width: 648,
      actions: [Strings.cancel, Strings.restart, Strings.shutdown],
      onAction: (action) {
        if (action == Strings.restart) {
          startupService.restartDevice();
          dispose();
        }
        if (action == Strings.shutdown) {
          startupService.shutdownDevice();
          dispose();
        }
      },
    ));
  }

  bool _isPrelistedApp(String url) =>
      appLaunchEntries.any((entry) => entry['url'] == url);

  bool _isLaunchError(String url) => errors[url] != null;

  void _clearError(String url, String errorType) {
    runInAction(() {
      errors.removeWhere(
          (key, value) => key == url && value[1].startsWith(errorType));
    });
  }

  void _onIdle({required bool idle}) => runInAction(() {
        if (preferencesService.showScreensaver) {
          if (idle) {
            isIdle = idle;
          } else {
            // Wait for the screen saver to be visible and running before closing
            // it.
            if (views.isNotEmpty &&
                topView.url == kScreenSaverUrl &&
                topView.loading) {
              closeView();
              isIdle = false;
            }
          }
        }
      });

  // This callback is triggered only for the views launched from the shell.
  void _onElementClosed(String id) {
    // Find the view with id that was terminated without closing its view. There
    // two scenarios where this can happen:
    // - VIEW_LOADED: The underlying component self exited or was killed from
    //   terminal or it crashed AFTER the view was loaded. Currently there is no
    //   way to distinguish the actual reason. Filed https://fxbug.dev/83165
    // - VIEW_NOT_LOADED: The view did not load because the component url is
    //   invalid. Ideally, this would be handled by ElementController throwing
    //   a [ProposeElementError.NOT_FOUND].
    //  TODO(https://fxbug.dev/83164): Handle ProposeElementError once
    //  implemented.
    final stuckViews = views.where((view) => view.id == id);
    if (stuckViews.isNotEmpty) {
      final view = stuckViews.first;
      if (view.loaded) {
        view.close();
      } else {
        runInAction(() => appIsLaunching.value = false);
        final description = Strings.applicationFailedToStart(view.title);
        _displayDialog(AlertDialogInfo(
          title: description,
          body: 'Url: ${view.url}',
          defaultAction: Strings.close,
          actions: [Strings.close],
          onClose: view.close,
        ));
      }
    }
  }

  Map<String, dynamic> _getInspectData() {
    final data = <String, dynamic>{};
    // Overlays currently visible.
    data['appBarVisible'] = appBarVisible;
    data['sideBarVisible'] = sideBarVisible;
    data['overlaysVisible'] = overlaysVisible;
    data['lastAction'] = shortcutsService.lastShortcutAction;
    data['darkMode'] = hasDarkTheme;

    // Number of running component views.
    data['numViews'] = views.length;

    if (views.isNotEmpty) {
      // List of views that are currently running.
      for (int i = 0; i < views.length; i++) {
        final view = views[i];

        // Active (focused) view.
        if (view == topView) {
          data['activeView'] = i;
        }

        // View title, url, focused and viewport.
        data['view-$i'] = <String, dynamic>{};
        final viewData = data['view-$i'];
        viewData['title'] = view.title;
        viewData['url'] = view.url;
        viewData['focused'] = view.view == _focusedView.value;

        final viewport = view.viewport;
        if (viewport != null) {
          viewData['viewportLTRB'] = [
            viewport.left,
            viewport.top,
            viewport.right,
            viewport.bottom,
          ].join(',');
        }
      }
    }
    return data;
  }

  void _displayDialog(DialogInfo dialog) {
    runInAction(() {
      // Override the onClose method to allow removal of dialog from dialogs.
      final closeFn = dialog.onClose;
      dialog.onClose = () {
        closeFn?.call();
        runInAction(() => dialogs.remove(dialog));
      };

      dialogs.add(dialog);
      if (viewsVisible) {
        // Set focus to shell view so that we can receive the esc key press.
        setFocusToShellView();
      }
    });
  }

  // Adds inspect data when requested by [Inspect].
  void _onInspect(Node node, [Map<String, dynamic>? inspectData]) {
    final data = inspectData ?? _getInspectData();
    for (final entry in data.entries) {
      if (entry.value is Map<String, dynamic>) {
        _onInspect(node.child(entry.key)!, entry.value);
      } else {
        switch (entry.value.runtimeType) {
          case bool:
            node.boolProperty(entry.key)!.setValue(entry.value);
            break;
          case int:
            node.intProperty(entry.key)!.setValue(entry.value);
            break;
          case double:
            node.doubleProperty(entry.key)!.setValue(entry.value);
            break;
          case String:
            node.stringProperty(entry.key)!.setValue(entry.value);
            break;
          default:
            assert(false, 'Invalid inspect type: ${entry.value.runtimeType}');
            break;
        }
      }
    }
  }

  void _onFeedbackSubmit(String uuid) {
    runInAction(() {
      _feedbackUuid.value = uuid;
      _feedbackPage.value = FeedbackPage.submitted;
    });
  }

  void _onFeedbackError(String error) {
    runInAction(() {
      _feedbackErrorMsg.value = error;
      _feedbackPage.value = FeedbackPage.failed;
    });
  }
}

class _AutomatorImpl implements Automator {
  final AppStateImpl state;
  _AutomatorImpl(this.state);

  @override
  Future<void> launch(String appName) async {
    final entry = state.appLaunchEntries
        .firstWhereOrNull((entry) => entry['title'] == appName);
    if (entry == null) {
      throw MethodException(AutomatorErrorCode.invalidArgs);
    }

    state.launch(entry['title']!, entry['url']!,
        alternateServiceName: entry['element_manager_name']);

    // Wait for the app to launch and receive focus.
    final completer = Completer();
    late ReactionDisposer disposer;
    disposer = reaction<ViewHandle?>((_) => state._focusedView.value, (view) {
      // Check if a child view received focus and it is the launched view.
      if (!state.shellHasFocus.value && state.topView.title == entry['title']) {
        completer.complete();
        disposer();
      }
    });
    // If the app does not launch in a reasonable time, throw failed exception.
    Future.delayed(
        Duration(seconds: 30),
        (() => completer
            .completeError(MethodException(AutomatorErrorCode.failed))));

    return completer.future;
  }

  @override
  Future<void> closeAll() async {
    state.closeAll();
  }
}
