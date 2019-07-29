// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:io';

import 'package:yaml/yaml.dart';

import 'models/settings_model.dart';
import 'setting_entry.dart';

/// [SettingEntryParser] interprets the setting configuration (most often
/// imported from the settings.config asset) into a list of [SettingEntry].
class SettingEntryParser {
  /// Keys found in the YAML configuration.
  static const String _keyTitle = 'title';
  static const String _keyIcon = 'icon';
  static const String _keyComponent = 'component';

  static Future<List<SettingEntry>> parseFile(
      SettingsModel model, String path) async {
    File configFile = File('pkg/data/settings.config');
    if (!configFile.existsSync()) {
      return [];
    }

    return parse(model, await configFile.readAsString());
  }

  static Future<List<SettingEntry>> parse(
      SettingsModel model, String yaml) async {
    YamlDocument config = loadYamlDocument(yaml);

    List<SettingEntry> entries = [];

    // Note that the configuration is a list of dictionaries in order to
    // preserve order. Otherwise, a top-level dictionary would suffice.
    config.contents.value.forEach((value) {
      value.forEach((key, value) {
        entries.add(ComponentSettingEntry(
            key,
            value[_keyTitle],
            value.containsKey(_keyIcon) ? value[_keyIcon] : null,
            value.containsKey(_keyComponent) ? value[_keyComponent] : null));
      });
    });

    return entries;
  }
}
