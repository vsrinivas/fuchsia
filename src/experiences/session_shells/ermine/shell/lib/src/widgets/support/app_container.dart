// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:flutter/material.dart';
import 'package:vector_math/vector_math_64.dart';

import '../../models/app_model.dart';
import '../../utils/styles.dart';

/// Defines a widget to manage the fullscreen state of the [App] widget.
class AppContainer extends StatelessWidget {
  final Widget child;
  final AppModel model;

  const AppContainer({@required this.child, @required this.model});

  @override
  Widget build(BuildContext context) {
    return AnimatedBuilder(
      animation: Listenable.merge([
        model.clustersModel.fullscreenStoryNotifier,
        model.peekNotifier,
      ]),
      child: child,
      builder: (context, child) {
        double top = model.isFullscreen && !model.peekNotifier.value
            ? -ErmineStyle.kTopBarHeight - ErmineStyle.kStoryTitleHeight
            : 0;
        return AnimatedContainer(
          transform: Matrix4.translation(Vector3(0, top, 0)),
          child: child,
          duration: ErmineStyle.kScreenAnimationDuration,
          curve: ErmineStyle.kScreenAnimationCurve,
        );
      },
    );
  }
}
