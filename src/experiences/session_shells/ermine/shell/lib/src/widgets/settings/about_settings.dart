// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:ermine/src/states/settings_state.dart';
import 'package:ermine/src/widgets/settings/setting_details.dart';
import 'package:flutter/material.dart';
import 'package:internationalization/strings.dart';

const _disclaimer =
    'Workstation is an open source reference design for Fuchsia. It’s intended as a developer tool to explore Fuchsia, a brand new operating system built from scratch.\n\nThis is a developer tool - not a consumer oriented product. This preview is intended for developers and enthusiasts to explore and experiment with, but does not come with strong security, privacy, or robustness guarantees.\n\nExpect bugs and rapid changes!\n\nPlease file bugs and send feedback to help improve Fuchsia!';

/// Defines a widget to show disclaimer about workstation in [SettingDetails] widget.
class AboutSettings extends StatelessWidget {
  final SettingsState state;

  const AboutSettings(this.state);

  @override
  Widget build(BuildContext context) {
    return SettingDetails(
      title: Strings.about,
      onBack: state.showAllSettings,
      child: Padding(
        padding: EdgeInsets.all(24),
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            Text(Strings.disclaimer,
                style: Theme.of(context).textTheme.subtitle1),
            SizedBox(height: 16),
            Text(_disclaimer, style: Theme.of(context).textTheme.bodyText2),
            // TODO(fxb/80893): Add updates & privacy
          ],
        ),
      ),
    );
  }
}
