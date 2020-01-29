// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:flutter/material.dart';

import '../../models/app_model.dart';
import '../../utils/elevations.dart';
import '../../utils/styles.dart';
import 'animation_driver.dart';

/// Defines a widget to manage the visibility of the keyboard help panel.
class KeyboardHelp extends StatelessWidget {
  /// The model that holds the state for this widget.
  final AppModel model;

  /// Constructor.
  const KeyboardHelp({@required this.model});

  @override
  Widget build(BuildContext context) {
    double bottom() => !model.isFullscreen || model.peekNotifier.value
        ? model.topbarModel.keyboardButtonRect.bottom
        : MediaQuery.of(context).size.height -
            ErmineStyle.kTopBarHeight -
            ErmineStyle.kStoryTitleHeight;
    return Stack(
      children: <Widget>[
        AnimatedBuilder(
          animation: model.helpVisibility,
          builder: (context, child) => model.helpVisibility.value
              ? Positioned(
                  bottom: bottom(),
                  right: model.topbarModel.keyboardButtonRect.right,
                  child: AnimationDriver(
                    tween:
                        Tween<Offset>(begin: Offset(0, 0), end: Offset(0, 1)),
                    builder: (context, animation) => FractionalTranslation(
                      translation: animation.value,
                      child: Material(
                        color: Colors.black,
                        elevation: Elevations.systemOverlayElevation,
                        child: Container(
                          decoration: BoxDecoration(
                              border: Border.all(
                            color: ErmineStyle.kOverlayBorderColor,
                            width: ErmineStyle.kOverlayBorderWidth,
                          )),
                          padding: EdgeInsets.all(16),
                          width: 400,
                          height: 500,
                          child: SingleChildScrollView(
                            child: Text(
                              model.keyboardShortcuts,
                              style: TextStyle(
                                fontFamily: 'RobotoMono',
                                fontSize: 14.0,
                              ),
                            ),
                          ),
                        ),
                      ),
                    ),
                  ),
                )
              : Offstage(),
        ),
      ],
    );
  }
}
