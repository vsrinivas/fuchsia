// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:flutter/material.dart';
import 'package:internationalization/strings.dart';
import 'package:intl/intl.dart';

import '../../models/topbar_model.dart';
import '../../utils/styles.dart';
import 'button.dart';

class Topbar extends StatelessWidget {
  final TopbarModel model;

  const Topbar({@required this.model});

  @override
  Widget build(BuildContext context) {
    return RepaintBoundary(
      child: Container(
        decoration: BoxDecoration(
          border: Border(
            bottom: BorderSide(
              color: ErmineStyle.kOverlayBorderColor,
              width: ErmineStyle.kOverlayBorderWidth,
            ),
          ),
          color: ErmineStyle.kOverlayBackgroundColor,
        ),
        height: ErmineStyle.kTopBarHeight,
        child: Stack(
          fit: StackFit.expand,
          children: <Widget>[
            Row(
              crossAxisAlignment: CrossAxisAlignment.stretch,
              children: <Widget>[
                // Overview.
                Button(
                  child: Text(Strings.overview.toUpperCase()),
                  decoration: BoxDecoration(
                    border: Border(
                      right: BorderSide(
                        color: ErmineStyle.kOverlayBorderColor,
                        width: ErmineStyle.kOverlayBorderWidth,
                      ),
                    ),
                  ),
                  onTap: model.showOverview,
                ),
                // Recents.
                Button(
                  child: Text(Strings.recents.toUpperCase()),
                  decoration: BoxDecoration(
                    border: Border(
                      right: BorderSide(
                        color: ErmineStyle.kOverlayBorderColor,
                        width: ErmineStyle.kOverlayBorderWidth,
                      ),
                    ),
                  ),
                  onTap: model.showRecents,
                ),

                Spacer(),

                // Status.
                Button(
                  key: model.statusButtonKey,
                  child: AnimatedBuilder(
                    animation: model.appModel.currentTime,
                    builder: (_, __) => Text(DateFormat.E()
                        .add_yMd()
                        .add_jm()
                        .format(model.appModel.currentTime.value)),
                  ),
                  decoration: BoxDecoration(
                    border: Border(
                      left: BorderSide(
                        color: ErmineStyle.kOverlayBorderColor,
                        width: ErmineStyle.kOverlayBorderWidth,
                      ),
                    ),
                  ),
                  onTap: model.showStatus,
                ),

                // Ask.
                Button(
                  key: model.askButtonKey,
                  child: Text(Strings.ask.toUpperCase()),
                  decoration: BoxDecoration(
                    border: Border(
                      left: BorderSide(
                        color: ErmineStyle.kOverlayBorderColor,
                        width: ErmineStyle.kOverlayBorderWidth,
                      ),
                    ),
                  ),
                  onTap: model.showAsk,
                ),

                // Keyboard help.
                Button(
                  key: model.keyboardButtonKey,
                  child: Icon(
                    Icons.keyboard,
                    color: Colors.white,
                  ),
                  decoration: BoxDecoration(
                    border: Border(
                      left: BorderSide(
                        color: ErmineStyle.kOverlayBorderColor,
                        width: ErmineStyle.kOverlayBorderWidth,
                      ),
                    ),
                  ),
                  onTap: model.showKeyboardHelp,
                ),
              ],
            ),
            // Story title.
            // Center(
            //   child: Text(strings.browser().toUpperCase(), textAlign: TextAlign.center),
            // ),
          ],
        ),
      ),
    );
  }
}
