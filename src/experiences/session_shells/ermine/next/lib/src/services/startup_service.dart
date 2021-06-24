// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:convert' show json;
import 'dart:io';
import 'dart:ui';

import 'package:fidl_fuchsia_buildinfo/fidl_async.dart';
import 'package:fidl_fuchsia_device_manager/fidl_async.dart';
import 'package:fidl_fuchsia_intl/fidl_async.dart';
import 'package:flutter/services.dart';
import 'package:fuchsia_internationalization_flutter/internationalization.dart';
import 'package:fuchsia_logger/logger.dart';
import 'package:fuchsia_scenic/views.dart';
import 'package:fuchsia_services/services.dart';

import 'package:next/src/utils/view_handle.dart';

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
    'url': 'fuchsia-pkg://fuchsia.com/terminal#meta/terminal.cmx',
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
class StartupService {
  /// Returns the shell's [ComponentContext].
  final ComponentContext componentContext;

  /// Returns the shell's [ViewHandle].
  final ViewHandle hostView;

  final _intl = PropertyProviderProxy();
  final _deviceManager = AdministratorProxy();
  final _provider = ProviderProxy();
  String _buildVersion = '--';
  late final List<Map<String, String>> appLaunchEntries;

  StartupService()
      : componentContext = ComponentContext.create(),
        hostView = ViewHandle(ScenicContext.hostViewRef()) {
    Incoming.fromSvcPath().connectToService(_intl);
    Incoming.fromSvcPath().connectToService(_deviceManager);
    Incoming.fromSvcPath().connectToService(_provider);

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
        appLaunchEntries = entries;
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
  }

  void dispose() {
    _deviceManager.ctrl.close();
    _intl.ctrl.close();
    _provider.ctrl.close();
  }

  void serve() {
    componentContext.outgoing.serveFromStartupInfo();
  }

  /// Return the build version.
  String get buildVersion => _buildVersion;

  /// Reboot the device.
  void restartDevice() => _deviceManager.suspend(suspendFlagReboot);

  /// Shutdown the device.
  void shutdownDevice() => _deviceManager.suspend(suspendFlagPoweroff);

  Stream<Locale> get stream => LocaleSource(_intl).stream();
}
