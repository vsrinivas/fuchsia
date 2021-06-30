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
import 'package:ermine/src/states/app_state.dart';
import 'package:ermine/src/states/oobe_state.dart';
import 'package:ermine/src/states/settings_state.dart';
import 'package:ermine/src/states/view_state.dart';
import 'package:ermine/src/states/view_state_impl.dart';
import 'package:ermine/src/utils/mobx_disposable.dart';
import 'package:ermine/src/utils/mobx_extensions.dart';
import 'package:ermine/src/utils/themes.dart';
import 'package:ermine/src/utils/view_handle.dart';
import 'package:flutter/material.dart' hide Action;
import 'package:fuchsia_logger/logger.dart';
import 'package:internationalization/strings.dart';
import 'package:mobx/mobx.dart';

/// Defines the implementation of [AppState].
class AppStateImpl with Disposable implements AppState {
  final FocusService focusService;
  final LaunchService launchService;
  final StartupService startupService;
  final PresenterService presenterService;
  final ShortcutsService shortcutsService;
  final PreferencesService preferencesService;
  final PointerEventsService pointerEventsService;

  static const kFeedbackUrl =
      'fuchsia-pkg://fuchsia.com/feedback_settings#meta/feedback_settings.cmx';
  static const kLicenseUrl =
      'fuchsia-pkg://fuchsia.com/license_settings#meta/license_settings.cmx';

  AppStateImpl({
    required this.startupService,
    required this.launchService,
    required this.focusService,
    required this.presenterService,
    required this.shortcutsService,
    required this.preferencesService,
    required this.pointerEventsService,
  }) : localeStream = startupService.stream.asObservable() {
    _focusedView = startupService.hostView.asObservable();
    focusService.onFocusMoved = _onFocusMoved;
    presenterService
      ..onViewPresented = _onViewPresented
      ..onViewDismissed = _onViewDismissed
      ..onError = _onPresentError
      ..advertise(startupService.componentContext.outgoing);

    // Register keyboard shortcuts and then initialize SettingsState with it.
    shortcutsService.register(_actions);
    settingsState = SettingsState.from(shortcutsService: shortcutsService);

    pointerEventsService
      ..onPeekBegin = _onPeekBegin
      ..onPeekEnd = _onPeekEnd;
    startupService.serve();

    // Add reactions to state changes.
    reactions.add(reaction<bool>((_) => views.isNotEmpty, (hasViews) {
      // Listen to out-of-band pointer events only when apps are launched.
      pointerEventsService.listen = hasViews;
      // Display overlays when no views are present.
      if (!hasViews) {
        overlayVisibility.value = true;
      }
    }));
  }

  @override
  void dispose() {
    super.dispose();

    settingsState.dispose();
    oobeState.dispose();

    startupService.dispose();
    focusService.dispose();
    presenterService.dispose();
    shortcutsService.dispose();
    preferencesService.dispose();
    pointerEventsService.dispose();
    settingsState.dispose();
  }

  @override
  late final SettingsState settingsState;

  @override
  OobeState get oobeState => OobeState.fromEnv();

  @override
  late final theme = (() {
    return preferencesService.darkMode.value
        ? AppTheme.darkTheme
        : AppTheme.lightTheme;
  }).asComputed();

  @override
  ObservableValue<bool> get hasDarkTheme => preferencesService.darkMode;

  /// Defines the visible status of overlays visible in 'overview' state. At the
  /// moment this is the [AppBar] and the [SideBar].
  final overlayVisibility = true.asObservable();

  /// A flag that is set to true when the [AppBar] should be peeking.
  final appBarPeeking = false.asObservable();

  /// A flag that is set to true when the [SideBar] should be peeking.
  final sideBarPeeking = false.asObservable();

  /// Returns true if shell has focus and any side bars are visible.
  @override
  late final overlaysVisible = (() {
    return !oobeVisible.value &&
        shellHasFocus.value &&
        (appBarVisible.value || sideBarVisible.value);
  }).asComputed();

  @override
  late final oobeVisible = () {
    return preferencesService.launchOobe.value;
  }.asComputed();

  @override
  late final appBarVisible = (() {
    return shellHasFocus.value &&
        views.isNotEmpty &&
        topView.value.ready.value &&
        (appBarPeeking.value || overlayVisibility.value);
  }).asComputed();

  @override
  late final sideBarVisible = (() {
    return shellHasFocus.value &&
        (sideBarPeeking.value || overlayVisibility.value);
  }).asComputed();

  @override
  final errors = <String, List<String>>{}.asObservable();

  @override
  final views = <ViewState>[].asObservable();

  @override
  final ObservableStream<Locale> localeStream;

  late final shellHasFocus = (() {
    return startupService.hostView == _focusedView.value;
  }).asComputed();

  @override
  late final Observable<ViewState> topView = Observable<ViewState>(views.first);

  @override
  String get buildVersion => startupService.buildVersion;

  @override
  List<Map<String, String>> get appLaunchEntries =>
      startupService.appLaunchEntries;

  void setFocusToShellView() {
    setFocus(startupService.hostView);
  }

  void setFocusToChildView() {
    if (views.isNotEmpty) {
      setFocus(topView.value.view);
    }
  }

  @override
  late final showOverlay = () {
    overlayVisibility.value = true;
    setFocusToShellView();
  }.asAction();

  @override
  late final Action hideOverlay = setFocusToChildView.asAction();

  @override
  late final Action showAppBar = () {
    overlayVisibility.value = true;
    showOverlay();
  }.asAction();

  @override
  late final Action showSideBar = () {
    overlayVisibility.value = true;
    showOverlay();
  }.asAction();

  @override
  late final switchNext = () {
    if (views.length > 1) {
      // Get next view from top view. Wrap to first view in list, if it is last.
      final nextView = topView.value == views.last
          ? views.first
          : views[views.indexOf(topView.value) + 1];
      topView.value = nextView;
      (nextView as ViewStateImpl).ready.value = false;
    }
  }.asAction();

  @override
  late final switchPrev = () {
    if (views.length > 1) {
      final prevView = topView.value == views.first
          ? views.last
          : views[views.indexOf(topView.value) - 1];
      topView.value = prevView;
      (prevView as ViewStateImpl).ready.value = false;
    }
  }.asAction();

  @override
  late final switchView = (ViewState view) {
    topView.value = view;
    (view as ViewStateImpl).ready.value = false;
  }.asAction();

  @override
  late final cancel = hideOverlay;

  @override
  late final Action closeView = () {
    if (views.isEmpty) {
      return;
    }
    topView.value.close();
  }.asAction();

  @override
  late final launch = (String title, String url) async {
    try {
      await launchService.launch(title, url);
      _clearError(url, 'ProposeElementError');
      // ignore: avoid_catches_without_on_clauses
    } catch (e) {
      _onLaunchError(url, e.toString());
      log.shout('$e: Failed to propose element <$url>');
    }
  }.asAction();

  @override
  late final Action launchFeedback = () async {
    try {
      await launchService.launch(Strings.feedback, kFeedbackUrl);
      _clearError(kFeedbackUrl, 'ProposeElementError');
      // ignore: avoid_catches_without_on_clauses
    } catch (e) {
      _onLaunchError(kFeedbackUrl, e.toString());
      log.shout('$e: Failed to propose element <$kFeedbackUrl>');
    }
  }.asAction();

  @override
  late final Action launchLicense = () async {
    try {
      await launchService.launch(Strings.feedback, kLicenseUrl);
      _clearError(kLicenseUrl, 'ProposeElementError');
      // ignore: avoid_catches_without_on_clauses
    } catch (e) {
      _onLaunchError(kLicenseUrl, e.toString());
      log.shout('$e: Failed to propose element <$kLicenseUrl>');
    }
  }.asAction();

  @override
  late final Action setTheme = (bool darkTheme) {
    preferencesService.darkMode.value = darkTheme;
  }.asAction();

  @override
  late final restart = startupService.restartDevice.asAction();

  @override
  late final shutdown = startupService.shutdownDevice.asAction();

  @override
  late final oobeFinished = () {
    preferencesService.launchOobe.value = false;
  }.asAction();

  // Map key shortcuts to corresponding actions.
  Map<String, VoidCallback> get _actions => {
        'launcher': showOverlay,
        'switchNext': switchNext,
        'switchPrev': switchPrev,
        'cancel': cancel,
        'close': closeView,
        'settings': showOverlay,
      };

  late final Observable<ViewHandle> _focusedView;
  void _onFocusMoved(ViewHandle viewHandle) {
    runInAction(() {
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
            topView.value = view;
            break;
          }
        }
      }
    });
    //}
  }

  void setFocus(ViewHandle view) {
    focusService.moveFocus(view);
  }

  bool _onViewPresented(ViewState viewState) {
    final view = viewState as ViewStateImpl;
    // Make this view the top view.
    runInAction(() {
      views.add(view);
      topView.value = view;

      // If any, remove previously cached launch errors for the app.
      if (viewState.url != null) {
        _clearError(viewState.url!, 'ViewControllerEpitaph');
      }
    });

    // Focus on view when it is ready.
    view.reactions.add(reaction<bool>((_) => view.ready.value, (ready) {
      if (ready && view == topView.value) {
        setFocus(view.view);
      }
    }));

    // Update view hittestability based on overlay visibility.
    view.reactions.add(reaction<bool>((_) => overlaysVisible.value, (overlay) {
      view.hitTestable.value = !overlay;
    }));

    // Remove view from views when it is closed.
    view.reactions.add(when((_) => view.closed.value, () {
      _onViewDismissed(view);
    }));
    return true;
  }

  // Called when a view is dismissed from an external source (not user).
  void _onViewDismissed(ViewState viewState) {
    runInAction(() {
      final view = viewState as ViewStateImpl;
      // Switch to next view before closing the view if it was the top view.
      if (view == topView.value) {
        switchNext();
      }

      views.remove(view);
      focusService.removeView(view.view);
      view.dispose();
    });
  }

  void _onPeekBegin(PeekEdge edge) {
    runInAction(() {
      appBarPeeking.value = edge == PeekEdge.left;
      sideBarPeeking.value = edge == PeekEdge.right;
      setFocusToShellView();
    });
  }

  void _onPeekEnd() {
    runInAction(() {
      appBarPeeking.value = false;
      sideBarPeeking.value = false;
      if (!overlayVisibility.value) {
        setFocusToChildView();
      }
    });
  }

  void _onPresentError(String url, String error) {
    runInAction(() {
      final errorSpec = error.split('.').last;
      final description = errorSpec == 'invalidViewSpec'
          ? 'A valid ViewHolderToken is missing'
          : errorSpec == 'rejected'
              ? 'The request to present the view is rejected'
              : 'Something went wrong while presenting the view';
      errors[url] = [
        description,
        '$error\nhttps://fuchsia.dev/reference/fidl/fuchsia.session#ViewControllerEpitaph'
      ];
    });
  }

  void _onLaunchError(String url, String error) {
    final proposeError = error.split(' ').last;
    print('Handling launch error $proposeError for $url');
    final errorSpec = proposeError.split('.').last;
    final description = errorSpec == 'notFound'
        ? 'The component URL could not be resolved'
        : errorSpec == 'rejected'
            ? 'The element spec is malformed'
            : 'Something went wrong while launching';

    errors[url] = [
      description,
      '$proposeError\nhttps://fuchsia.dev/reference/fidl/fuchsia.session#ProposeElementError'
    ];
  }

  void _clearError(String url, String errorType) {
    runInAction(() {
      errors.removeWhere(
          (key, value) => key == url && value[1].startsWith(errorType));
    });
  }
}
