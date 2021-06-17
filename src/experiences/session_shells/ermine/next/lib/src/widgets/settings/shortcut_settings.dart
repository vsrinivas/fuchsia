// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:flutter/material.dart';
import 'package:internationalization/strings.dart';

import 'package:next/src/states/settings_state.dart';
import 'package:next/src/widgets/settings/setting_details.dart';

/// Defines a widget to list all keyboard shortcuts in [SettingDetails] widget.
class ShortcutSettings extends StatelessWidget {
  final SettingsState state;

  const ShortcutSettings(this.state);

  @override
  Widget build(BuildContext context) {
    return SettingDetails(
      title: Strings.shortcuts,
      onBack: state.showAllSettings,
      child: ListView(
        children: state.shortcutBindings.entries
            .map((e) => ListTile(
                  title: Text(e.key),
                  subtitle: Text(e.value.join(', ')),
                  leading: Icon(Icons.keyboard_hide_outlined),
                ))
            .toList(),
      ),
    );
  }
}
