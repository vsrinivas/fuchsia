// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:convert' show json;
import 'dart:io';

import 'package:ermine_utils/ermine_utils.dart';
import 'package:mobx/mobx.dart';

/// Defines a service that allows reading and storing application data.
class PreferencesService with Disposable {
  static const kPreferencesJson = '/data/preferences.json';
  static const kStartupConfigJson = '/config/data/startup_config.json';

  // Use dark mode: true | false.
  final darkMode = true.asObservable();

  // Launch oobe: true | false.
  final launchOobe = false.asObservable();

  final Map<String, dynamic> _data;

  PreferencesService() : _data = _readPreferences() {
    darkMode.value = _data['dark_mode'] ?? true;
    launchOobe.value = _data['launch_oobe'] ?? _launchOobeFromConfig();
    reactions
      ..add(reaction<bool>((_) => darkMode.value, _setDarkMode))
      ..add(reaction<bool>((_) => launchOobe.value, _setLaunchOobe));
  }

  void _setDarkMode(bool value) {
    _data['dark_mode'] = value;
    _writePreferences(_data);
  }

  void _setLaunchOobe(bool value) {
    _data['launch_oobe'] = value;
    _writePreferences(_data);
  }

  static Map<String, dynamic> _readPreferences() {
    final file = File(kPreferencesJson);
    if (file.existsSync()) {
      return json.decode(file.readAsStringSync(), reviver: (key, value) {
        // Sanitize input.
        if (key == 'dark_mode') {
          return value is bool && value;
        }

        return value;
      });
    }
    return {};
  }

  static void _writePreferences(Map<String, dynamic> data) {
    File(kPreferencesJson).writeAsStringSync(json.encode(data));
  }

  static bool _launchOobeFromConfig() {
    final file = File(kStartupConfigJson);
    if (file.existsSync()) {
      final data = json.decode(file.readAsStringSync());
      return data['launch_oobe'] ?? false;
    }
    return false;
  }
}
