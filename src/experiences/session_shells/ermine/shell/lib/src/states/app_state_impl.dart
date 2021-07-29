// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:isolate';
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
import 'package:fuchsia_inspect/inspect.dart';
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
  static const kScreenSaverUrl =
      'fuchsia-pkg://fuchsia.com/screensaver#meta/screensaver.cmx';

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
      ..onPresenterDisposed = dispose
      ..onViewPresented = _onViewPresented
      ..onViewDismissed = _onViewDismissed
      ..onError = _onPresentError
      ..advertise(startupService.componentContext.outgoing);

    // Register keyboard shortcuts and then initialize SettingsState with it.
    shortcutsService.register(_actions);
    settingsState = SettingsState.from(shortcutsService: shortcutsService);

    pointerEventsService
      ..onPeekBegin = _onPeekBegin
      ..onPeekEnd = _onPeekEnd
      ..onActivity = () => startupService.onActivity('pointer');

    startupService
      ..onInspect = _onInspect
      ..onIdle = _onIdle
      ..onAltReleased = _triggerSwitch
      ..serve();

    // Add reactions to state changes.
    reactions
      ..add(reaction<bool>((_) => views.isNotEmpty, (hasViews) {
        // Listen to out-of-band pointer events only when apps are launched.
        pointerEventsService.listen = hasViews;
        // Display overlays when no views are present.
        if (!hasViews) {
          overlayVisibility.value = true;
        }
      }))
      ..add(when((_) => oobeVisible.value, () async {
        // Start oobe component.
        try {
          final elementController = await launchService.launch(
              'Oobe', 'fuchsia-pkg://fuchsia.com/oobe#meta/oobe.cmx');
          await elementController.ctrl.whenClosed;
          // ignore: avoid_catches_without_on_clauses
        } catch (e) {
          // If OOBE launch fails, it is a fatal error. Quit the shell.
          log.severe('Failed to launch OOBE. Quiting.');
          Isolate.current.kill();
        }
        oobeFinished();
      }))
      ..add(reaction<bool>((_) => isIdle.value, (idle) async {
        if (idle) {
          // Start screenSaver.
          await launchService.launch('Screen Saver', kScreenSaverUrl);
        }
      }));
  }

  @override
  void dispose() {
    super.dispose();

    settingsState.dispose();
    _oobeState?.dispose();

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

  OobeState? _oobeState;
  @override
  OobeState get oobeState => _oobeState ??= OobeState.fromEnv();

  @override
  final Observable<bool> isIdle = false.asObservable();

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

  @override
  late final alertsVisible = (() {
    return alerts.isNotEmpty;
  }).asComputed();

  /// Returns true if shell has focus and any side bars are visible.
  @override
  late final overlaysVisible = (() {
    return !oobeVisible.value &&
        !isIdle.value &&
        shellHasFocus.value &&
        (appBarVisible.value ||
            sideBarVisible.value ||
            switcherVisible.value ||
            alertsVisible.value);
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
  final alerts = <AlertInfo>[].asObservable();

  @override
  final errors = <String, List<String>>{}.asObservable();

  @override
  final views = <ViewState>[].asObservable();

  @override
  final Observable<bool> switcherVisible = false.asObservable();

  @override
  late final viewsVisible = () {
    return views.isNotEmpty && !isIdle.value;
  }.asComputed();

  @override
  final ObservableStream<Locale> localeStream;

  late final shellHasFocus = (() {
    return startupService.hostView == _focusedView.value;
  }).asComputed();

  @override
  late final Observable<ViewState> topView = Observable<ViewState>(views.first);

  @override
  final Observable<ViewState?> switchTarget = Observable<ViewState?>(null);

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
  late final Action showAppBar = showOverlay;

  @override
  late final Action showSideBar = showOverlay;

  @override
  late final switchView = (ViewState view) {
    topView.value = view;
    (view as ViewStateImpl).ready.value = false;
  }.asAction();

  @override
  late final switchNext = () {
    if (views.length > 1) {
      // Start with the top view.
      switchTarget.value ??= topView.value;

      // Get next view from top view. Wrap to first view in list, if it is last.
      switchTarget.value = switchTarget.value == views.last
          ? views.first
          : views[views.indexOf(switchTarget.value) + 1];

      // Set focus to shell view so that we can receive the final Alt key press.
      setFocusToShellView();

      // Display the app switcher after shell has focus.
      when((_) => shellHasFocus.value, () => switcherVisible.value = true);
    }
  }.asAction();

  @override
  late final switchPrev = () {
    if (views.length > 1) {
      // Start with the top view.
      switchTarget.value ??= topView.value;

      switchTarget.value = switchTarget.value == views.first
          ? views.last
          : views[views.indexOf(switchTarget.value) - 1];

      // Set focus to shell view so that we can receive the final Alt key press.
      setFocusToShellView();

      // Display the app switcher after shell has focus.
      when((_) => shellHasFocus.value, () => switcherVisible.value = true);
    }
  }.asAction();

  void _triggerSwitch() {
    if (switchTarget.value != null) {
      runInAction(() {
        if (switchTarget.value != topView.value) {
          topView.value = switchTarget.value!;
          (switchTarget.value as ViewStateImpl).ready.value = false;
        } else {
          // Set focus to the child view since we did not switch to another view.
          setFocusToChildView();
        }

        // Dismiss the app switcher after shell loses focus
        when((_) => !shellHasFocus.value, () => switcherVisible.value = false);

        switchTarget.value = null;
      });
    }
  }

  @override
  late final cancel = hideOverlay;

  @override
  late final Action closeView = () {
    if (views.isEmpty || oobeVisible.value) {
      return;
    }
    topView.value.close();
  }.asAction();

  late final Action closeAll = () {
    for (final view in views) {
      view.close();
    }
    views.clear();
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
    launch([Strings.feedback, kFeedbackUrl]);
  }.asAction();

  @override
  late final Action launchLicense = () async {
    launch([Strings.license, kLicenseUrl]);
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

  late final showScreenSaver = () {
    _onIdle(idle: true);
  }.asAction();

  // Map key shortcuts to corresponding actions.
  Map<String, VoidCallback> get _actions => {
        'launcher': showOverlay,
        'switchNext': switchNext,
        'switchPrev': switchPrev,
        'cancel': cancel,
        'close': closeView,
        'closeAll': closeAll,
        'settings': showOverlay,
        'shortcuts': showOverlay,
        'screenSaver': showScreenSaver,
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

      // If the child view is the screen saver, make it non-focusable in order
      // for keyboard input to get routed to the shell and dismiss it.
      viewState.focusable.value = viewState.url != kScreenSaverUrl;

      // If any, remove previously cached launch errors for the app.
      if (viewState.url != null) {
        _clearError(viewState.url!, 'ViewControllerEpitaph');
      }
    });

    // Focus on view when it is ready.
    view.reactions.add(reaction<bool>((_) => view.ready.value, (ready) {
      if (ready && view == topView.value && view.focusable.value) {
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
      // Switch to next view before closing this view if it was the top view
      // and there are other views.
      if (view == topView.value && views.length > 1) {
        final nextView = topView.value == views.last
            ? views.first
            : views[views.indexOf(topView.value) + 1];
        topView.value = nextView;
        (nextView as ViewStateImpl).ready.value = false;
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
          ? Strings.invalidViewSpecDesc
          : errorSpec == 'rejected'
              ? Strings.viewPresentRejectedDesc
              : Strings.defaultPresentErrorDesc;
      const referenceLink =
          'https://fuchsia.dev/reference/fidl/fuchsia.session#ViewControllerEpitaph';

      if (_isPrelistedApp(url)) {
        errors[url] = [description, '$error\n$referenceLink'];
      } else {
        int index = alerts.length;
        alerts.add(AlertInfo(
          title: description,
          content: '${Strings.errorWhilePresenting},\n$url\n\n'
              '${Strings.errorType}: $error\n\n'
              '${Strings.moreErrorInformation}\n$referenceLink',
          buttons: {
            Strings.close: () {
              alerts.removeAt(index);
            }
          },
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

    errors[url] = [
      description,
      '$proposeError\nhttps://fuchsia.dev/reference/fidl/fuchsia.session#ProposeElementError'
    ];
  }

  bool _isPrelistedApp(String url) =>
      appLaunchEntries.any((entry) => entry['url'] == url);

  void _clearError(String url, String errorType) {
    runInAction(() {
      errors.removeWhere(
          (key, value) => key == url && value[1].startsWith(errorType));
    });
  }

  void _onIdle({required bool idle}) => runInAction(() {
        if (idle) {
          isIdle.value = idle;
        } else {
          // Wait for the screen saver to be visible and running before closing
          // it.
          if (views.isNotEmpty &&
              topView.value.url == kScreenSaverUrl &&
              topView.value.ready.value) {
            closeView();
            isIdle.value = false;
          }
        }
      });

  // Adds inspect data when requested by [Inspect].
  void _onInspect(Node node) {
    // Overlays currently visible.
    node.boolProperty('appBarVisible')!.setValue(appBarVisible.value);
    node.boolProperty('sideBarVisible')!.setValue(sideBarVisible.value);
    node.boolProperty('overlaysVisible')!.setValue(overlaysVisible.value);

    // Number of running component views.
    node.intProperty('numViews')?.setValue(views.length);

    if (views.isNotEmpty) {
      // List of views that are currently running.
      for (int i = 0; i < views.length; i++) {
        final view = views[i];

        // Active (focused) view.
        if (view == topView.value) {
          node.intProperty('activeView')?.setValue(i);
        }

        // View title, url, focused and viewport.
        final viewNode = node.child('view-$i')!;
        viewNode.stringProperty('title')!.setValue(view.title);
        viewNode.stringProperty('url')!.setValue(view.url!);
        viewNode.boolProperty('focused')!.setValue(view == topView.value);

        final viewport = view.viewport;
        if (viewport != null) {
          viewNode.stringProperty('viewportLTRB')!.setValue([
                viewport.left,
                viewport.top,
                viewport.right,
                viewport.bottom,
              ].join(','));
        }
      }
    }
  }
}
