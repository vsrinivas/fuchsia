// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TODO(http://fxbug.dev/107480): Resolve lint issues and reenable analysis for file
// ignore_for_file: deprecated_member_use

import 'package:ermine/src/states/app_state.dart';
import 'package:ermine/src/states/settings_state.dart';
import 'package:ermine/src/widgets/settings/setting_details.dart';
import 'package:flutter/material.dart';
import 'package:flutter_mobx/flutter_mobx.dart';
import 'package:internationalization/strings.dart';

/// Defines a widget to provide data sharing consent opt-in/out capability.
class DataSharingConsentSettings extends StatelessWidget {
  final AppState app;
  final SettingsState state;

  const DataSharingConsentSettings(this.app, this.state);

  @override
  Widget build(BuildContext context) => Observer(builder: (_) {
        final bodyTextStyle = Theme.of(context).textTheme.bodyText1!;
        final privacyTermsUrl = app.simpleLocale.isEmpty
            ? 'https://policies.google.com/privacy'
            : 'https://policies.google.com/privacy?hl=${app.simpleLocale}';
        return SettingDetails(
          title: Strings.usageAndDiagnostics,
          onBack: state.showAllSettings,
          trailing: Switch(
            value: state.dataSharingConsentEnabled,
            onChanged: (value) {
              state.setDataSharingConsent(enabled: value);
            },
          ),
          child: Padding(
            padding: EdgeInsets.all(24),
            child: Column(
              crossAxisAlignment: CrossAxisAlignment.start,
              children: [
                Text(
                  Strings.helpFuchsia,
                  style: bodyTextStyle,
                ),
                SizedBox(height: 16),
                Tooltip(
                  message: privacyTermsUrl,
                  child: TextButton(
                    style: TextButton.styleFrom(padding: EdgeInsets.zero),
                    onPressed: () =>
                        app.launch(Strings.privacyTerms, privacyTermsUrl),
                    child: Container(
                      padding: EdgeInsets.only(bottom: 1),
                      decoration: BoxDecoration(
                        border: Border(
                          bottom: BorderSide(
                            color: Theme.of(context).dividerColor,
                            width: 1,
                          ),
                        ),
                      ),
                      child: Text(Strings.privacyTerms, style: bodyTextStyle),
                    ),
                  ),
                ),
              ],
            ),
          ),
        );
      });
}
