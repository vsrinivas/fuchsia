// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:async';

import 'package:flutter/material.dart';

import 'package:fidl_fuchsia_ui_brightness/fidl_async.dart';
import 'package:fidl_fuchsia_ui_remotewidgets/fidl_async.dart';
import 'package:fuchsia_services/services.dart' show StartupContext;
import 'package:quickui/quickui.dart';

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
  static const _title = 'Brightness';
  static const _buttonLabel = 'Auto';
  static const progressAction = 1;
  static const autoAction = 2;

  _BrightnessModel _model;

  Brightness(ControlProxy control) : super(null) {
    _model = _BrightnessModel(control: control, onChange: _onChange);
  }

  factory Brightness.fromStartupContext(StartupContext context) {
    final control = ControlProxy();
    context.incoming.connectToService(control);
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
        value.progress.action == progressAction) {
      _model.brightness = value.progress.value;
    } else if (value.$tag == ValueTag.button &&
        value.button.action == autoAction) {
      _model.auto = true;
    }
  }

  @override
  void dispose() {
    _model.dispose();
  }

  static Spec _specForAutoBrightness(double value) {
    return Spec(groups: [
      Group(title: _title, values: [
        Value.withIcon(IconValue(codePoint: Icons.brightness_auto.codePoint)),
        Value.withProgress(ProgressValue(value: value, action: progressAction)),
        Value.withText(TextValue(text: _buttonLabel)),
      ]),
    ]);
  }

  static Spec _specForManualBrightness(double value) {
    return Spec(groups: [
      Group(title: _title, values: [
        Value.withIcon(IconValue(codePoint: Icons.brightness_low.codePoint)),
        Value.withProgress(ProgressValue(value: value, action: progressAction)),
        Value.withIcon(IconValue(codePoint: Icons.brightness_high.codePoint)),
        Value.withButton(ButtonValue(label: _buttonLabel, action: autoAction)),
      ]),
    ]);
  }
}

class _BrightnessModel {
  static const _checkBrightnessDuration = Duration(seconds: 1);

  final ControlProxy control;
  final VoidCallback onChange;

  bool _auto;
  bool _enabled = false;
  double _brightness;
  StreamSubscription _brightnessSubscription;

  _BrightnessModel({this.control, this.onChange}) {
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
    onChange?.call();
  }

  double get brightness => _brightness;
  set brightness(double value) {
    _brightness = value;
    control.setManualBrightness(value);
    onChange?.call();
  }

  void _listen() async {
    _brightnessSubscription =
        Stream.periodic(_checkBrightnessDuration).listen((_) async {
      final brightness = await control.watchCurrentBrightness();
      if (brightness != _brightness) {
        _brightness = brightness;
        onChange?.call();
        _listen();
      }
    });
  }
}
