// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:flutter/gestures.dart';
import 'package:flutter/material.dart';
import 'package:flutter_mobx/flutter_mobx.dart';
import 'package:internationalization/strings.dart';
import 'package:oobe/src/states/oobe_state.dart';

/// Defines a widget to configure software update channels.
class DataSharing extends StatelessWidget {
  final OobeState oobe;

  const DataSharing(this.oobe);

  @override
  Widget build(BuildContext context) {
    return Padding(
      padding: EdgeInsets.all(16),
      child: FocusScope(
        child: Observer(builder: (context) {
          return Column(
            crossAxisAlignment: CrossAxisAlignment.stretch,
            children: [
              // Title.
              Text(
                oobe.privacyVisible
                    ? Strings.privacyPolicyTitle
                    : Strings.dataSharingTitle,
                textAlign: TextAlign.center,
                style: Theme.of(context).textTheme.headline3,
              ),

              // Description.
              if (!oobe.privacyVisible)
                Container(
                  alignment: Alignment.center,
                  padding: EdgeInsets.all(24),
                  child: SizedBox(
                    width: 600,
                    child: RichText(
                      text: TextSpan(
                        text: Strings.dataSharingDesc1,
                        style: Theme.of(context)
                            .textTheme
                            .bodyText1!
                            .copyWith(height: 1.55),
                        children: [
                          WidgetSpan(
                            alignment: PlaceholderAlignment.baseline,
                            baseline: TextBaseline.alphabetic,
                            child: TextButton(
                              onPressed: oobe.showPrivacy,
                              style: TextButton.styleFrom(
                                padding: EdgeInsets.zero,
                                minimumSize: Size.zero,
                              ),
                              child: Text(
                                Strings.dataSharingDesc2,
                                style: TextStyle(
                                  shadows: [
                                    Shadow(
                                      color: Theme.of(context)
                                              .textTheme
                                              .bodyText1!
                                              .copyWith(height: 1.55)
                                              .color ??
                                          Colors.white,
                                      offset: Offset(0, -3),
                                    )
                                  ],
                                  color: Colors.transparent,
                                  decoration: TextDecoration.underline,
                                  decorationColor: Colors.white,
                                ),
                              ),
                            ),
                          ),
                          TextSpan(text: Strings.dataSharingDesc3),
                        ],
                      ),
                      textAlign: TextAlign.center,
                    ),
                  ),
                ),

              Expanded(
                child: oobe.privacyVisible
                    ? Container(
                        alignment: Alignment.center,
                        margin: EdgeInsets.all(24),
                        child: SizedBox(
                          width: 600,
                          child: SingleChildScrollView(
                            child: Text(
                              oobe.privacyPolicy,
                              style: Theme.of(context)
                                  .textTheme
                                  .bodyText1!
                                  .copyWith(height: 1.55),
                            ),
                          ),
                        ),
                      )
                    : Offstage(),
              ),

              // Buttons.
              Container(
                alignment: Alignment.center,
                padding: EdgeInsets.all(24),
                child: oobe.privacyVisible
                    ? OutlinedButton(
                        autofocus: true,
                        onPressed: oobe.hidePrivacy,
                        child: Text(Strings.close.toUpperCase()),
                      )
                    : Row(
                        mainAxisAlignment: MainAxisAlignment.center,
                        children: [
                          // Back button.
                          OutlinedButton(
                            onPressed: oobe.prevScreen,
                            child: Text(Strings.back.toUpperCase()),
                          ),
                          SizedBox(width: 24),
                          // Disagree button.
                          OutlinedButton(
                            onPressed: oobe.disagree,
                            child: Text(Strings.disagree.toUpperCase()),
                          ),
                          SizedBox(width: 24),
                          // Agree button.
                          OutlinedButton(
                            autofocus: true,
                            onPressed: oobe.agree,
                            child: Text(Strings.agree.toUpperCase()),
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
