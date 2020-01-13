// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:flutter/material.dart';

import '../../models/app_model.dart';
import '../../utils/elevations.dart';
import '../../utils/styles.dart';
import '../support/animation_driver.dart';
import 'ask.dart';

/// Defines a widget to manage the visibility of the [Ask] widget.
class AskContainer extends StatelessWidget {
  /// The model that holds the state for this widget.
  final AppModel model;

  /// Constructor.
  const AskContainer({@required this.model});

  @override
  Widget build(BuildContext context) {
    double bottom() => !model.isFullscreen || model.peekNotifier.value
        ? model.topbarModel.askButtonRect.bottom
        : MediaQuery.of(context).size.height -
            ErmineStyle.kTopBarHeight -
            ErmineStyle.kStoryTitleHeight;
    return AnimatedBuilder(
      animation: model.askVisibility,
      child: Material(
        color: ErmineStyle.kBackgroundColor,
        elevation: Elevations.systemOverlayElevation,
        child: Ask(
          key: model.askKey,
          suggestionService: model.suggestions,
          onDismiss: () => model.askVisibility.value = false,
        ),
      ),
      builder: (context, child) => model.askVisibility.value
          ? Positioned(
              bottom: bottom(),
              right: model.topbarModel.askButtonRect.right,
              width: ErmineStyle.kAskBarWidth,
              child: AnimationDriver(
                tween: Tween<Offset>(begin: Offset.zero, end: Offset(0, 1)),
                curve: ErmineStyle.kScreenAnimationCurve,
                duration: ErmineStyle.kScreenAnimationDuration,
                builder: (context, animation) => FractionalTranslation(
                  translation: animation.value,
                  child: child,
                ),
              ),
            )
          : Offstage(),
    );
  }
}
