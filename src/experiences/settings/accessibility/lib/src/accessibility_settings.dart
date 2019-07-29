// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:flutter/widgets.dart';
import 'package:flutter/material.dart';
import 'package:lib.settings/widgets.dart';
import 'package:lib.widgets/model.dart';

import 'accessibility_settings_model.dart';

class AccessibilitySettings extends StatelessWidget {
  @override
  Widget build(BuildContext context) => Provide<AccessibilitySettingsModel>(
          builder: (
        BuildContext context,
        Widget child,
        AccessibilitySettingsModel model,
      ) =>
              LayoutBuilder(
                  builder: (BuildContext context, BoxConstraints constraints) =>
                      Material(
                          child: _buildAccessibilitySettingPage(
                              // TODO: Replace the scale value with a proper solution.
                              constraints.maxHeight > 360.0 ? 1.0 : 0.5,
                              model))));

  SettingsPage _buildAccessibilitySettingPage(
      double scale, AccessibilitySettingsModel model) {
    final screenReaderSetting = SettingsSwitchTile(
      scale: scale,
      state: model.screenReaderEnabled.value,
      text: 'Screen Reader',
      onSwitch: (value) => model.screenReaderEnabled.value = value,
    );

    final colorInversionSetting = SettingsSwitchTile(
      scale: scale,
      state: model.colorInversionEnabled.value,
      text: 'Color Inversion',
      onSwitch: (value) => model.colorInversionEnabled.value = value,
    );

    final magnificationSetting = SettingsSwitchTile(
      scale: scale,
      state: model.magnificationEnabled.value,
      text: 'Magnification',
      onSwitch: (value) => model.magnificationEnabled.value = value,
    );

    final magnificationZoomSetting = SettingsText(
        scale: scale,
        text: 'Magnification zoom: ${model.magnificationZoom.value}%');

    return SettingsPage(scale: scale, sections: [
      SettingsSection(
          title: 'Accessibility Settings',
          scale: scale,
          child: SettingsItemList(
            items: [
              screenReaderSetting,
              colorInversionSetting,
              magnificationSetting,
              magnificationZoomSetting
            ],
          ))
    ]);
  }
}
