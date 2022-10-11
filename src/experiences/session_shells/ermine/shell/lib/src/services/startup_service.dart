// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:async';
import 'dart:convert' show json;
import 'dart:io';
import 'dart:isolate';

import 'package:ermine_utils/ermine_utils.dart';
import 'package:fidl_fuchsia_buildinfo/fidl_async.dart' as buildinfo;
import 'package:fidl_fuchsia_hardware_power_statecontrol/fidl_async.dart';
import 'package:fidl_fuchsia_intl/fidl_async.dart';
import 'package:fidl_fuchsia_power_button/fidl_async.dart';
import 'package:fidl_fuchsia_ui_activity/fidl_async.dart' as activity;
import 'package:flutter/services.dart';
import 'package:flutter/widgets.dart' hide Action;
import 'package:fuchsia_inspect/inspect.dart';
import 'package:fuchsia_internationalization_flutter/internationalization.dart';
import 'package:fuchsia_logger/logger.dart';
import 'package:fuchsia_scenic/views.dart';
import 'package:fuchsia_services/services.dart';

// List of default app entries to use when app_launch_entries.json is not found.
const _kAppDefaultEntries = <Map<String, String>>[
  {
    'title': 'Simple Browser',
    'icon': 'images/SimpleBrowser-icon-2x.png',
    'url': 'fuchsia-pkg://fuchsia.com/simple-browser#meta/simple-browser.cmx',
  },
  {
    'title': 'Terminal',
    'icon': 'images/Terminal-icon-2x.png',
    'url': 'fuchsia-pkg://fuchsia.com/terminal#meta/terminal.cm',
  },
  {
    'title': 'Settings',
    'icon': 'images/Settings-icon-2x.png',
  },
];

/// Defines a service that manages the applications startup state like
/// [ComponentContext] and view's [ViewHandle].
///
/// It also provides access to:
/// - listening to change in system [Locale].
/// - build version of the system.
/// - restart/shutdown the system.
/// - load MaterialIcons [Font] at startup.
class StartupService extends activity.Listener {
  /// Global flag that enables screen saver to kick in. This is set to false
  /// when flutter driver is enabled. See [test_main.dart].
  static bool allowScreensaver = true;

  /// Returns the shell's [ComponentContext].
  final ComponentContext componentContext;

  /// Returns the shell's [ViewHandle].
  final ViewHandle hostView;

  /// Callback to service [Inspect] requests from the system.
  late final void Function(Node) onInspect;

  /// Callback when the system is idle according to activity service.
  late final void Function({required bool idle}) onIdle;

  /// Callback when the Alt key was released. Used to dismiss app switching ui.
  late final VoidCallback onAltReleased;

  /// Callback when the keyboard power button was pressed.
  late final VoidCallback onPowerBtnPressed;

  final _inspect = Inspect();
  final _intl = PropertyProviderProxy();
  final _hardwareAdmin = AdminProxy();
  final _provider = buildinfo.ProviderProxy();
  final _activity = activity.ProviderProxy();
  final _activityBinding = activity.ListenerBinding();
  final _powerBtnMonitor = MonitorProxy();

  String _buildVersion = '--';
  late final List<Map<String, String>> appLaunchEntries;

  StartupService()
      : componentContext = ComponentContext.create(),
        hostView = ViewHandle(ScenicContext.hostViewRef()) {
    Incoming.fromSvcPath().connectToService(_hardwareAdmin);
    Incoming.fromSvcPath().connectToService(_intl);
    Incoming.fromSvcPath().connectToService(_provider);
    Incoming.fromSvcPath().connectToService(_activity);
    Incoming.fromSvcPath().connectToService(_powerBtnMonitor);

    if (allowScreensaver) {
      _activity.watchState(_activityBinding.wrap(this));
    }

    // TODO(http://fxb/80131): Remove once activity is reported in the input
    // pipeline.
    WidgetsFlutterBinding.ensureInitialized().addPostFrameCallback((_) {
      RawKeyboard.instance.addListener((event) {
        // We use Alt key release event to dismiss app switching UI.
        final data = event.data as RawKeyEventDataFuchsia;
        final fuchsiaKey = data.hidUsage;
        if (fuchsiaKey == PhysicalKeyboardKey.altLeft.usbHidUsage ||
            fuchsiaKey == PhysicalKeyboardKey.altRight.usbHidUsage) {
          if (!event.isAltPressed) {
            onAltReleased();
          }
        }
        // Notify activity service of user input. This is used to dismiss the
        // screen saver if it is active. We do this only for key presses
        // because key release from screensaver shortcut itself might cancel it.
        if (event is RawKeyDownEvent) {
          onActivity('keyboard');
        }
      });
    });

    // We cannot load MaterialIcons font file from pubspec.yaml. So load it
    // explicitly.
    File file = File('/pkg/data/MaterialIcons-Regular.otf');
    if (file.existsSync()) {
      FontLoader('MaterialIcons')
        ..addFont(() async {
          return file.readAsBytesSync().buffer.asByteData();
        }())
        ..load();
    }

    // Get app launch entries.
    file = File('/pkg/data/app_launch_entries.json');
    if (file.existsSync()) {
      final data = file.readAsStringSync();
      final entries = json.decode(data, reviver: (key, value) {
        // Sanitize and strongly type json values.
        if (value is Map<String, dynamic>) {
          return Map<String, String>.from(value);
        } else if (value is List<dynamic>) {
          return List<Map<String, String>>.from(value);
        } else {
          return value;
        }
      });
      try {
        // Filter out entries missing 'title', the minimum requirement.
        appLaunchEntries = (entries as List<Map<String, String>>)
            .where((e) => e.containsKey('title'))
            .toList(growable: false);
        // ignore: avoid_catches_without_on_clauses
      } catch (e) {
        log.warning('$e: Failed to parse app_launch_entries.json. \n$data');
        appLaunchEntries = _kAppDefaultEntries;
      }
    } else {
      appLaunchEntries = _kAppDefaultEntries;
    }

    // Get the build info.
    _provider.getBuildInfo().then((buildInfo) {
      _buildVersion = buildInfo.version ?? '--';
    });

    // Monitor the power button and display a dialog when it is pressed.
    _initPowerBtnAction();
    _powerBtnMonitor.onButtonEvent.listen((event) {
      if (event == PowerButtonEvent.press) {
        log.info('The keyboard power button is pressed.');
        onPowerBtnPressed();
      }
    });
  }

  void _initPowerBtnAction() async {
    await _powerBtnMonitor.setAction(Action.ignore);
    final action = await _powerBtnMonitor.getAction();
    log.info(
        'Set the power button action to ${action == Action(0) ? 'IGNORE' : 'SHUTDOWN'}');
  }

  void dispose() {
    _hardwareAdmin.ctrl.close();
    _intl.ctrl.close();
    _provider.ctrl.close();
    _activityBinding.close();
    _activity.ctrl.close();
  }

  /// Publish outgoing services.
  void serve() {
    _inspect
      ..serve(componentContext.outgoing)
      ..onDemand('ermine', onInspect);
    componentContext.outgoing.serveFromStartupInfo();
  }

  // The time when last activity was reported.
  DateTime? _lastActivityReport;

  /// Report pointer and keyboard interaction to activity tracker service.
  void onActivity(String type) {
    // Throttle activity reporting by 5 seconds.
    if (_lastActivityReport == null ||
        DateTime.now()
            .subtract(Duration(seconds: 5))
            .isAfter(_lastActivityReport!)) {
      _lastActivityReport = DateTime.now();
    }
    // Also exit from idle state.
    onIdle(idle: false);
    // Since we have user activity, cancel the timer that disables startup idle.
    _disableIdleAtStartupTimer?.cancel();
  }

  /// Return the build version.
  String get buildVersion => _buildVersion;

  /// Reboot the device.
  void restartDevice() =>
      _hardwareAdmin.reboot(RebootReason.userRequest).catchError((_) {});

  /// Shutdown the device.
  void shutdownDevice() => _hardwareAdmin.poweroff().catchError((_) {});

  /// Logout of the user shell.
  void logout() {
    // Exit the current isolate, which allows the parent to treat it as a logout
    // action.
    WidgetsBinding.instance.addPostFrameCallback((_) {
      Isolate.current.setErrorsFatal(true);
      Isolate.current.kill(priority: Isolate.beforeNextEvent);
    });
  }

  Stream<Locale> get stream => LocaleSource(_intl).stream();

  Timer? _disableIdleAtStartupTimer;

  @override
  Future<void> onStateChanged(activity.State state, int transitionTime) async {
    // TODO(http://fxb/80131): Ignore the idle state at startup.
    if (_disableIdleAtStartupTimer == null && state == activity.State.idle) {
      // Initiate idle state at 15 minutes ourselves.
      _disableIdleAtStartupTimer =
          Timer(Duration(minutes: 15), () => onIdle(idle: true));
      return;
    }
    // Subsequent state changes from activity service don't need startup idle.
    _disableIdleAtStartupTimer?.cancel();

    onIdle(idle: state == activity.State.idle);
  }
}
