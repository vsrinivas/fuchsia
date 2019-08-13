// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:flutter/material.dart';

import '../../models/app_model.dart';
import '../../utils/styles.dart';
import '../animation_driver.dart';
import 'ask.dart';

/// Defines a widget to manage the visibility of the [Ask] widget.
class AskContainer extends StatelessWidget {
  /// The model that holds the state for this widget.
  final AppModel model;

  /// Constructor.
  const AskContainer({@required this.model});

  @override
  Widget build(BuildContext context) {
    return AnimatedBuilder(
      animation: model.askVisibility,
      child: Ask(model: model.askModel),
      builder: (context, child) => model.askVisibility.value
          ? Positioned(
              bottom: MediaQuery.of(context).size.height,
              right: 0,
              width: 500,
              child: AnimationDriver(
                tween: Tween<Offset>(begin: Offset(0, 0), end: Offset(0, 1)),
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
