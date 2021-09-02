// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:convert';
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
import 'package:ermine/src/states/settings_state.dart';
import 'package:ermine/src/states/view_state.dart';
import 'package:ermine/src/states/view_state_impl.dart';
import 'package:ermine_utils/ermine_utils.dart';
import 'package:flutter/material.dart' hide Action;
import 'package:fuchsia_inspect/inspect.dart';
import 'package:fuchsia_logger/logger.dart';
import 'package:fuchsia_scenic_flutter/fuchsia_view.dart';
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
  }) : _localeStream = startupService.stream.asObservable() {
    launchService.onControllerClosed = _onElementClosed;
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
      ..add(when((_) => oobeVisible, () async {
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
      ..add(reaction<bool>((_) => isIdle, (idle) async {
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

  @override
  bool get alertsVisible => _alertsVisible.value;
  late final _alertsVisible = (() {
    return alerts.isNotEmpty;
  }).asComputed();

  /// Returns true if shell has focus and any side bars are visible.
  @override
  bool get overlaysVisible => _overlaysVisible.value;
  late final _overlaysVisible = (() {
    return !oobeVisible &&
        !isIdle &&
        !appIsLaunching.value &&
        shellHasFocus.value &&
        (appBarVisible || sideBarVisible || switcherVisible || alertsVisible);
  }).asComputed();

  @override
  bool get oobeVisible => _oobeVisible.value;
  late final _oobeVisible = () {
    return preferencesService.launchOobe.value;
  }.asComputed();

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
  final alerts = <AlertInfo>[].asObservable();

  @override
  final errors = <String, List<String>>{}.asObservable();

  @override
  final views = <ViewState>[].asObservable();

  @override
  bool get switcherVisible => _switcherVisible.value;
  set switcherVisible(bool visible) => _switcherVisible.value = visible;
  final Observable<bool> _switcherVisible = false.asObservable();

  @override
  bool get viewsVisible => _viewsVisible.value;
  late final _viewsVisible = () {
    return views.isNotEmpty && !isIdle;
  }.asComputed();

  @override
  Locale? get locale => _localeStream.value;
  final ObservableStream<Locale> _localeStream;

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
  void cancel() => hideOverlay();

  @override
  void closeView() => _closeView();
  late final Action _closeView = () {
    if (views.isEmpty || oobeVisible) {
      return;
    }
    topView.close();
  }.asAction();

  late final Action closeAll = () {
    for (final view in views) {
      view.close();
    }
    views.clear();
  }.asAction();

  @override
  void launch(String title, String url) => _launch([title, url]);
  late final _launch = (String title, String url) async {
    try {
      _clearError(url, 'ProposeElementError');
      await launchService.launch(title, url);
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

  @override
  void launchLicense() => launch(Strings.license, kLicenseUrl);

  @override
  void setTheme({bool darkTheme = true}) => _setTheme([darkTheme]);
  late final Action _setTheme = (bool darkTheme) {
    preferencesService.darkMode.value = darkTheme;
  }.asAction();

  @override
  void restart() => runInAction(startupService.restartDevice);

  @override
  void shutdown() => runInAction(startupService.shutdownDevice);

  @override
  void oobeFinished() =>
      runInAction(() => preferencesService.launchOobe.value = false);

  @override
  void updateChannelAlert() {
    runInAction(() {
      int index = alerts.length;
      alerts.add(AlertInfo(
        title: Strings.channelUpdateAlertTitle,
        content: Strings.channelUpdateAlertBody,
        buttons: {
          Strings.close: () {
            alerts.removeAt(index);
          },
          Strings.continueLabel: () {
            settingsState.checkForUpdates();
            alerts.removeAt(index);
          },
        },
      ));
    });
  }

  late final showScreenSaver = () {
    _onIdle(idle: true);
  }.asAction();

  // Map key shortcuts to corresponding actions.
  Map<String, dynamic> get _actions => {
        'launcher': showOverlay,
        'switchNext': switchNext,
        'switchPrev': switchPrev,
        'cancel': cancel,
        'close': closeView,
        'closeAll': closeAll,
        'settings': showOverlay,
        'shortcuts': showOverlay,
        'screenSaver': showScreenSaver,
        'inspect': () => json.encode(_getInspectData()),
      };

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

      // If the child view is the screen saver, make it non-focusable in order
      // for keyboard input to get routed to the shell and dismiss it.
      viewState.focusable = viewState.url != kScreenSaverUrl;

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
    view.reactions.add(reaction<bool>((_) => overlaysVisible, (overlay) {
      // Don't reset hittest flag when showing app switcher, because the
      // app switcher does not react to pointer events.
      view.hitTestable = !overlay || switcherVisible;
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
      // Switch to next view before closing this view if it was the top view
      // and there are other views.
      if (view == topView && views.length > 1) {
        final nextView = topView == views.last
            ? views.first
            : views[views.indexOf(topView) + 1];
        topView = nextView;
        setFocusToChildView();
      }

      views.remove(view);
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
      // TODO(https://fxbug.dev/83165): Ignore rendered/loaded views.
      if (view.loaded) {
        return;
      }
      final description = Strings.applicationFailedToStart(view.title);
      alerts.add(AlertInfo(
        title: description,
        content: 'Url: ${view.url}',
        buttons: {
          Strings.close: () {
            alerts.removeAt(0);
            view.close();
          }
        },
      ));
    }
  }

  Map<String, dynamic> _getInspectData() {
    final data = <String, dynamic>{};
    // Overlays currently visible.
    data['appBarVisible'] = appBarVisible;
    data['sideBarVisible'] = sideBarVisible;
    data['overlaysVisible'] = overlaysVisible;

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
        viewData['focused'] = view == topView;

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
}
