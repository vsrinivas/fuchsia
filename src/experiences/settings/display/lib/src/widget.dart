// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:lib.widgets/model.dart';
import 'package:flutter/material.dart';
import 'package:flutter/widgets.dart';
import 'package:lib.display.flutter/display_policy_brightness_model.dart';
import 'package:lib.settings/widgets.dart';

Widget _buildDisplaySettings(
    {@required DisplayPolicyBrightnessModel model, @required double scale}) {
  return SettingsPage(scale: scale, sections: [_buildBrightness(model, scale)]);
}

SettingsSection _buildBrightness(
    DisplayPolicyBrightnessModel model, double scale) {
  return SettingsSection(
      title: 'Brightness',
      scale: scale,
      child: Slider(
        activeColor: Colors.grey[600],
        inactiveColor: Colors.grey[200],
        value: model.brightness,
        onChanged: (double brightness) => model.brightness = brightness,
        min: DisplayPolicyBrightnessModel.minLevel,
        max: DisplayPolicyBrightnessModel.maxLevel,
        divisions: 9,
      ));
}

/// Widget that displays system settings such as update.
class DisplaySettings extends StatelessWidget {
  const DisplaySettings();

  @override
  Widget build(BuildContext context) =>
      ScopedModelDescendant<DisplayPolicyBrightnessModel>(
          builder: (
        BuildContext context,
        Widget child,
        DisplayPolicyBrightnessModel model,
      ) =>
              LayoutBuilder(
                  builder: (BuildContext context, BoxConstraints constraints) =>
                      Material(
                          child: _buildDisplaySettings(
                              model: model,
                              scale:
                                  constraints.maxHeight > 360.0 ? 1.0 : 0.5))));
}
