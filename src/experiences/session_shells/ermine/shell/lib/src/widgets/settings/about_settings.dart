// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TODO(http://fxbug.dev/107480): Resolve lint issues and reenable analysis for file
// ignore_for_file: deprecated_member_use

import 'package:ermine/src/states/settings_state.dart';
import 'package:ermine/src/widgets/settings/setting_details.dart';
import 'package:flutter/material.dart';
import 'package:internationalization/strings.dart';

/// Defines a widget to show disclaimer about workstation in [SettingDetails] widget.
class AboutSettings extends StatelessWidget {
  final SettingsState state;

  const AboutSettings(this.state);

  @override
  Widget build(BuildContext context) {
    return SettingDetails(
      title: Strings.aboutFuchsia,
      onBack: state.showAllSettings,
      child: ListView(
        padding: EdgeInsets.all(24),
        children: [
          Text(Strings.disclaimer,
              style: Theme.of(context).textTheme.subtitle1),
          SizedBox(height: 16),
          Text(Strings.disclaimerText,
              style: Theme.of(context).textTheme.bodyText2),
          if (state.optedIntoUpdates == true) ...[
            SizedBox(height: 16),
            Text(Strings.updatesAndPrivacy,
                style: Theme.of(context).textTheme.subtitle1),
            SizedBox(height: 16),
            Text(Strings.updatesAndPrivacyText,
                style: Theme.of(context).textTheme.bodyText2),
          ],
        ],
      ),
    );
  }
}
