// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:flutter/material.dart';
import 'package:lib.widgets/model.dart';

import '../models/settings_model.dart';
import '../setting_entry.dart';

/// Main view that shows all settings.
class AllSettings extends StatelessWidget {
  final List<SettingEntry> _settingEntries;

  const AllSettings(this._settingEntries);

  List<Widget> _buildSettingList(
      BuildContext context, SettingsModel settingsModel) {
    List<Widget> rows = [];

    for (SettingEntry entry in _settingEntries) {
      rows.add(entry.getRow(settingsModel, context));
    }

    return rows;
  }

  @override
  Widget build(BuildContext context) {
    return ScopedModelDescendant<SettingsModel>(
      builder: (
        BuildContext context,
        Widget child,
        SettingsModel settingsModel,
      ) =>
          Scaffold(
            appBar: AppBar(
              title: Text('All Settings'),
            ),
            body: ListView(
              physics: BouncingScrollPhysics(),
              children: _buildSettingList(context, settingsModel),
            ),
          ),
    );
  }
}
