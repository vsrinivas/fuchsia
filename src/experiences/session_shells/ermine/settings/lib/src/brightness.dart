// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:async';

import 'package:fidl_fuchsia_ui_brightness/fidl_async.dart';
import 'package:fidl_fuchsia_ui_remotewidgets/fidl_async.dart';
import 'package:flutter/material.dart';
import 'package:fuchsia_services/services.dart' show Incoming;
import 'package:internationalization/strings.dart';
import 'package:quickui/quickui.dart';

// ignore_for_file: prefer_constructors_over_static_methods

/// Defines a [UiSpec] for controlling screen brightness.
///
/// * If the system does not have hardware for brightness control, no [Spec] is
///   generated.
/// * If brightness is set to AUTO, show slider followed by AUTO label:
///         ----------------------
///   {A}   |////////////        |       AUTO
///         ----------------------
/// * If brighness is MANUAL, show slider followed by AUTO button:
///         ----------------------
///   {b}   |////////////        |  {B} {AUTO}
///         ----------------------
/// * {A} - Auto icon, {b} Brightness min icon, {B} Brightness max icon.
class Brightness extends UiSpec {
  static const progressAction = 1;
  static const autoAction = 2;

  // Localized strings.
  static String get _title => Strings.brightness;
  static String get _auto => Strings.auto;

  late _BrightnessModel _model;

  Brightness(ControlProxy control) {
    _model = _BrightnessModel(control: control, onChange: _onChange);
  }

  factory Brightness.withSvcPath() {
    final control = ControlProxy();
    Incoming.fromSvcPath().connectToService(control);
    return Brightness(control);
  }

  void _onChange() {
    spec = _model.auto
        ? _specForAutoBrightness(_model.brightness)
        : _specForManualBrightness(_model.brightness);
  }

  @override
  void update(Value value) async {
    if (value.$tag == ValueTag.progress &&
        value.progress!.action == progressAction) {
      _model.brightness = value.progress!.value;
    } else if (value.$tag == ValueTag.button &&
        value.button!.action == autoAction) {
      _model.auto = true;
    }
  }

  @override
  void dispose() {
    _model.dispose();
  }

  static Spec _specForAutoBrightness(double value) {
    return Spec(title: _title, groups: [
      Group(
        title: _title,
        icon: IconValue(codePoint: Icons.brightness_auto.codePoint),
        values: [
          Value.withProgress(
              ProgressValue(value: value, action: progressAction)),
          Value.withText(TextValue(text: _auto)),
        ],
      ),
    ]);
  }

  static Spec _specForManualBrightness(double value) {
    return Spec(title: _title, groups: [
      Group(
          title: _title,
          icon: IconValue(codePoint: Icons.brightness_5.codePoint),
          values: [
            Value.withIcon(
                IconValue(codePoint: Icons.brightness_low.codePoint)),
            Value.withProgress(
                ProgressValue(value: value, action: progressAction)),
            Value.withIcon(
                IconValue(codePoint: Icons.brightness_high.codePoint)),
            Value.withButton(ButtonValue(label: _auto, action: autoAction)),
          ]),
    ]);
  }
}

class _BrightnessModel {
  final ControlProxy control;
  final VoidCallback onChange;

  late bool _auto;
  late bool _enabled = false;
  late double _brightness;
  late StreamSubscription _brightnessSubscription;

  _BrightnessModel({required this.control, required this.onChange}) {
    control.watchAutoBrightness().then((auto) {
      _enabled = true;
      _auto = auto;
      _listen();
    });
  }

  void dispose() {
    control.ctrl.close();
    _brightnessSubscription.cancel();
  }

  bool get enabled => _enabled;

  bool get auto => _auto;
  set auto(bool value) {
    _auto = value;
    control.setAutoBrightness();
    onChange();
  }

  double get brightness => _brightness;
  set brightness(double value) {
    _brightness = value;
    _auto = false;
    control.setManualBrightness(value);
    onChange();
  }

  void _listen() {
    _brightnessSubscription =
        control.watchCurrentBrightness().asStream().listen((brightness) {
      _brightnessSubscription.cancel();
      _brightness = brightness;
      onChange();
      _listen();
    });
  }
}
