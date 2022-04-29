// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:flutter/material.dart';
import 'package:flutter_mobx/flutter_mobx.dart';
import 'package:internationalization/strings.dart';
import 'package:login/src/states/oobe_state.dart';
import 'package:login/src/widgets/header.dart';
import 'package:mobx/mobx.dart';

/// Defines a widget to configure usage and diagnostic data sharing preference.
class DataSharing extends StatelessWidget {
  final OobeState oobe;
  final isOptedIn = true.asObservable();

  DataSharing(this.oobe);

  @override
  Widget build(BuildContext context) {
    return Padding(
      padding: EdgeInsets.all(16),
      child: FocusScope(
        child: Observer(builder: (context) {
          return Column(
            crossAxisAlignment: CrossAxisAlignment.stretch,
            children: [
              Header(
                title: Strings.dataSharingTitle,
                description: Strings.dataSharingDesc,
              ),
              SizedBox(height: 48),
              Expanded(
                child: Column(
                  children: [
                    Container(
                      width: 696,
                      padding: EdgeInsets.all(24),
                      decoration: BoxDecoration(
                        border:
                            Border.all(color: Theme.of(context).dividerColor),
                      ),
                      child: Row(
                        crossAxisAlignment: CrossAxisAlignment.center,
                        children: [
                          Checkbox(
                            value: isOptedIn.value,
                            onChanged: (value) => runInAction(
                                () => isOptedIn.value = value == true),
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
                      Strings.dataSharingPrivacyTerms(
                          'policies.google.com/privacy'),
                      style: Theme.of(context).textTheme.bodyText1,
                    ),
                  ],
                ),
              ),

              // Buttons.
              Container(
                alignment: Alignment.center,
                padding: EdgeInsets.all(24),
                child: Row(
                  mainAxisAlignment: MainAxisAlignment.center,
                  children: [
                    // Back button.
                    OutlinedButton(
                      onPressed: oobe.prevScreen,
                      child: Text(Strings.back),
                    ),
                    SizedBox(width: 24),
                    // Next button.
                    ElevatedButton(
                      autofocus: true,
                      onPressed: () {
                        oobe
                          ..setPrivacyConsent(consent: isOptedIn.value)
                          ..nextScreen();
                      },
                      child: Text(Strings.next),
                    ),
                  ],
                ),
              ),
            ],
          );
        }),
      ),
    );
  }
}
