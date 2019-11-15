// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:async';

import 'package:flutter/material.dart';

import 'package:fidl_fuchsia_media/fidl_async.dart' as vol;
import 'package:fidl_fuchsia_ui_remotewidgets/fidl_async.dart';
import 'package:internationalization/strings.dart';
import 'package:fuchsia_services/services.dart' show StartupContext;
import 'package:quickui/quickui.dart';

/// Defines a [UiSpec] for controlling device volume.
class Volume extends UiSpec {
  static const minVolumeAction = 1;
  static const maxVolumeAction = 2;
  static const changeVolumeAction = 3;

  // Localized strings.
  static String get _title => Strings.volume;
  static String get _min => Strings.min;
  static String get _max => Strings.max;

  _VolumeModel model;

  Volume(vol.AudioCoreProxy control) {
    model = _VolumeModel(control: control, onChange: _onChange);
  }

  factory Volume.fromStartupContext(StartupContext context) {
    final control = vol.AudioCoreProxy();
    context.incoming.connectToService(control);
    return Volume(control);
  }

  void _onChange() {
    spec = _specForVolume(model.volume);
  }

  @override
  void update(Value value) async {
    if (value.$tag == ValueTag.button) {
      if (value.button.action == minVolumeAction) {
        model.volume = 0;
      } else if (value.button.action == maxVolumeAction) {
        model.volume = 1;
      }
    } else if (value.$tag == ValueTag.progress) {
      model.volume = value.progress.value;
    }
  }

  @override
  void dispose() {
    model.dispose();
  }

  static Spec _specForVolume(double value) {
    String roundedVolume = (value * 100).round().toString();
    return Spec(title: _title, groups: [
      Group(title: _title, values: [
        Value.withText(TextValue(text: roundedVolume)),
        Value.withProgress(
            ProgressValue(value: value, action: changeVolumeAction)),
        Value.withButton(ButtonValue(label: _min, action: minVolumeAction)),
        Value.withButton(ButtonValue(label: _max, action: maxVolumeAction)),
      ]),
    ]);
  }
}

class _VolumeModel {
  static const double _minLevelGainDb = -45.0;
  static const double _maxLevelGainDb = 0.0;

  final vol.AudioCoreProxy control;
  final VoidCallback onChange;

  double _volume;
  StreamSubscription _volumeSubscription;

  _VolumeModel({this.control, this.onChange}) {
    _volumeSubscription = control.systemGainMuteChanged.listen((response) {
      volume = gainToLevel(response.gainDb);
    });
  }

  void dispose() {
    _volumeSubscription.cancel();
  }

  double get volume => _volume;
  set volume(double value) {
    _volume = value;
    control.setSystemGain(levelToGain(value));
    if (_volume == 0) {
      control.setSystemMute(true);
    } else {
      control.setSystemMute(false);
    }
    onChange?.call();
  }

  /// Converts a gain in db to an audio 'level' in the range 0.0 to 1.0
  /// inclusive.
  double gainToLevel(double gainDb) {
    if (gainDb <= _minLevelGainDb) {
      return 0.0;
    } else if (gainDb >= _maxLevelGainDb) {
      return 1.0;
    } else {
      //double ratio = gainDb / -_minLevelGainDb;
      return 1.0 - gainDb / _minLevelGainDb;
    }
  }

  /// Converts an audio 'level' in the range 0.0 to 1.0 inclusive to a gain in
  /// db.
  double levelToGain(double level) {
    if (level <= 0.0) {
      return _minLevelGainDb;
    } else if (level >= 1.0) {
      return _maxLevelGainDb;
    } else {
      return (1.0 - level) * _minLevelGainDb;
    }
  }
}
