// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:flutter/material.dart';
import 'package:internationalization/strings.dart';
import 'package:shell_settings/src/states/settings_state.dart';
import 'package:shell_settings/src/widgets/setting_details.dart';

/// Defines a widget to list all timezones in [SettingDetails] widget.
class TimezoneSettings extends StatelessWidget {
  final SettingsState state;
  final ValueChanged<String> onChange;

  const TimezoneSettings({required this.state, required this.onChange});

  @override
  Widget build(BuildContext context) {
    final timezones = state.timezones;
    return SettingDetails(
      title: Strings.timezone,
      onBack: state.showAllSettings,
      child: ListView.builder(
          itemCount: timezones.length,
          itemBuilder: (context, index) {
            final timezone =
                timezones[index].replaceAll('_', ' ').replaceAll('/', ' / ');
            return ListTile(
              title: Text(timezone),
              subtitle: index == 0 ? Text(Strings.selected) : null,
              leading: Icon(Icons.place),
              onTap: () => onChange(timezones[index]),
            );
          }),
    );
  }
}
