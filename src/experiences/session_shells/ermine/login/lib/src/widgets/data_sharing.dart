// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TODO(http://fxbug.dev/107480): Resolve lint issues and reenable analysis for file
// ignore_for_file: deprecated_member_use

import 'package:flutter/material.dart';
import 'package:flutter_mobx/flutter_mobx.dart';
import 'package:internationalization/strings.dart';
import 'package:login/src/states/oobe_state.dart';
import 'package:login/src/widgets/details.dart';
import 'package:mobx/mobx.dart';

/// Defines a widget to configure usage and diagnostic data sharing preference.
class DataSharing extends StatelessWidget {
  final OobeState oobe;
  final isOptedIn = true.asObservable();

  DataSharing(this.oobe);

  @override
  Widget build(BuildContext context) {
    return Observer(
      builder: (context) => Details(
        // Header: Title and description.
        title: Strings.dataSharingTitle,
        description: Strings.dataSharingDesc,

        // Content: Opt-in/out checkbox and privacy policy URL.
        scrollableContent: Column(
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            Container(
              padding: EdgeInsets.all(24),
              decoration: BoxDecoration(
                border: Border.all(color: Theme.of(context).dividerColor),
              ),
              child: Row(
                crossAxisAlignment: CrossAxisAlignment.center,
                children: [
                  Checkbox(
                    value: isOptedIn.value,
                    onChanged: (value) =>
                        runInAction(() => isOptedIn.value = value == true),
                    checkColor: Theme.of(context).bottomAppBarColor,
                  ),
                  SizedBox(width: 8),
                  Flexible(
                    child: Text(
                      Strings.dataSharingCheckboxLabel,
                      style: Theme.of(context).textTheme.bodyText1,
                    ),
                  ),
                ],
              ),
            ),
            SizedBox(height: 40),
            Text(
              Strings.dataSharingPrivacyTerms('policies.google.com/privacy'),
              style: Theme.of(context).textTheme.bodyText1,
            ),
          ],
        ),

        // Next button.
        buttons: [
          ElevatedButton(
            autofocus: true,
            onPressed: () {
              oobe
                ..setPrivacyConsent(consent: isOptedIn.value)
                ..nextScreen();
            },
            child: Text(Strings.next.toUpperCase()),
          ),
        ],
      ),
    );
  }
}
