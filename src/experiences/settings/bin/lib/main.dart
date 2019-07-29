// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:async';
import 'dart:developer' show Timeline;

import 'package:flutter/material.dart' hide Intent;
import 'package:fuchsia_logger/logger.dart';

import 'src/models/settings_model.dart';
import 'src/setting_entry.dart';
import 'src/setting_entry_parser.dart';
import 'src/widgets/all_settings.dart';

/// Main function of settings.
Future<Null> main() async {
  setupLogger(name: 'settings');
  Timeline.instantSync('settings starting');

  SettingsModel settingsModel = SettingsModel();
  List<SettingEntry> entries = await SettingEntryParser.parseFile(
      settingsModel, 'pkg/data/settings.config');

  // We handle license setting separately as it is not a component.
  entries.add(LicenseSettingEntry());

  Widget app = MaterialApp(
    home: AllSettings(entries),
    routes: _buildRoutes(settingsModel, entries),
  );

  app = ScopedModel<SettingsModel>(
    model: settingsModel,
    child: app,
  );

  runApp(app);

  Timeline.instantSync('settings started');
}

Map<String, WidgetBuilder> _buildRoutes(
    SettingsModel settingsModel, List<SettingEntry> entries) {
  Map<String, WidgetBuilder> routes = {};

  for (SettingEntry entry in entries) {
    routes[entry.route] = entry.getRouteBuilder(settingsModel);
  }

  return routes;
}
