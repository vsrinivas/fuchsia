// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:convert' show json;
import 'dart:io';

import 'package:ermine_utils/ermine_utils.dart';
import 'package:flutter/widgets.dart';
import 'package:fuchsia_logger/logger.dart';
import 'package:mobx/mobx.dart';

/// Defines a service that allows reading and storing application data.
class PreferencesService with Disposable {
  /// The JSON file that stores preferences persistently.
  static const kPreferencesJson = '/data/preferences.json';
  static const kDarkModeKey = 'dark_mode';
  static const kScaleKey = 'scale';
  static const kScreensaverKey = 'screensaver';
  static const kUserFeedbackStartupKey = 'show_user_feedback_startup';
  static const kScaleLowerBound = 0.25;
  static const kScaleUpperBound = 4.0;

  /// The JSON file that provides preferences as part of package install.
  static const kStartupPreferencesJson = '/pkg/data/preferences.json';

  // Use dark mode: true | false.
  final darkMode = true.asObservable();

  // Show screensaver: true | false.
  bool showScreensaver = false;

  // Show user feedback startup dialog: true | false
  final showUserFeedbackStartUpDialog = true.asObservable();

  // UI scaling.
  final _scale = 1.0.asObservable();

  final Map<String, dynamic> _data;

  PreferencesService() : _data = _readPreferences() {
    darkMode.value = _data[kDarkModeKey] ?? true;
    showScreensaver = _data[kScreensaverKey] ?? true;
    showUserFeedbackStartUpDialog.value =
        _data[kUserFeedbackStartupKey] ?? true;
    _scale.value = _data[kScaleKey] ?? _initialScale();
    reactions
      ..add(reaction<bool>((_) => darkMode.value, _setDarkMode))
      ..add(reaction<bool>((_) => showUserFeedbackStartUpDialog.value,
          _setUserFeedbackStartUpDialog));
  }

  void _setDarkMode(bool value) {
    _data[kDarkModeKey] = value;
    _writePreferences(_data);
  }

  void _setUserFeedbackStartUpDialog(bool value) {
    _data[kUserFeedbackStartupKey] = value;
    _writePreferences(_data);
  }

  double get scale => _scale.value;
  set scale(double newValue) {
    runInAction(() {
      // Only accept sane values.
      if (0.25 <= newValue && newValue <= 4.0) {
        _scale.value = newValue;
        _data[kScaleKey] = newValue;
        _writePreferences(_data);
      }
    });
  }

  void zoomIn() => scale += 0.5;

  void zoomOut() => scale -= 0.5;

  // TODO(https://fxbug.dev/62096): Remove once hardware resolution is supported.
  // Set the initial scale that results in a 1k (1080) height.
  static double _initialScale() {
    final scale =
        WidgetsFlutterBinding.ensureInitialized().window.physicalSize.height /
            1080;
    return scale;
  }

  static Map<String, dynamic> _readPreferences() {
    Map<String, dynamic> parsePreferences(String data) {
      return json.decode(data, reviver: (key, value) {
        // Sanitize input.
        if (key == kDarkModeKey) {
          return value is bool && value;
        }

        // Screensaver.
        if (key == kScreensaverKey) {
          // ignore: avoid_bool_literals_in_conditional_expressions
          return value is bool ? value : true;
        }

        // User feedback startup dialog.
        if (key == kUserFeedbackStartupKey) {
          // ignore: avoid_bool_literals_in_conditional_expressions
          return value is bool ? value : true;
        }

        // Scale.
        if (key == kScaleKey) {
          return value is double
              ? value.clamp(kScaleLowerBound, kScaleUpperBound)
              : _initialScale();
        }

        return value;
      });
    }

    final result = <String, dynamic>{};
    // Read preferences from package configuration.
    var file = File(kStartupPreferencesJson);
    if (file.existsSync()) {
      result.addAll(parsePreferences(file.readAsStringSync()));
    }

    // Read preferences from previous session and overwrite intial values.
    file = File(kPreferencesJson);
    if (file.existsSync()) {
      result.addAll(parsePreferences(file.readAsStringSync()));
      log.info('Read settings from previous session');
    } else {
      log.info('Failed to read settings from previous session');
    }

    return result;
  }

  static void _writePreferences(Map<String, dynamic> data) {
    File(kPreferencesJson).writeAsStringSync(json.encode(data), flush: true);
  }
}
