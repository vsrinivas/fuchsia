// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:flutter/material.dart';

import '../../models/app_model.dart';
import '../../utils/styles.dart';

/// Defines a widget that holds the [Oobe] widget and manages its animation.
class OobeContainer extends StatelessWidget {
  final AppModel model;

  const OobeContainer({@required this.model});

  @override
  Widget build(BuildContext context) {
    return Oobe(model: model);
  }
}

/// Defines a class that display the Oobe system overlay.
class Oobe extends StatelessWidget {
  final AppModel model;

  const Oobe({@required this.model});

  @override
  Widget build(BuildContext context) {
    return Container(
        padding: ErmineStyle.kOverviewElementPadding,
        child: Column(
          children: <Widget>[
            Expanded(
              child: Row(
                children: <Widget>[
                  Center(
                    child: Text(
                      'Welcome to Workstation OOBE'.toUpperCase(),
                      style: TextStyle(
                        color: Colors.white,
                        fontFamily: 'Roboto Mono',
                        fontSize: 48.0,
                      ),
                    ),
                  )
                ],
              ),
            ),
            Row(
              children: <Widget>[
                Spacer(),

                // Exit
                GestureDetector(
                  onTap: model.exitOobe,
                  child: Container(
                    alignment: Alignment.bottomRight,
                    height: ErmineStyle.kTopBarHeight,
                    child: Text('exit'.toUpperCase()),
                  ),
                ),
              ],
            ),
          ],
        ));
  }
}
