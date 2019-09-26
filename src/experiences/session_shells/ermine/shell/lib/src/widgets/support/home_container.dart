// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:flutter/material.dart';
import 'package:vector_math/vector_math_64.dart';

import '../../models/app_model.dart';
import '../../utils/styles.dart';
import 'home.dart';

/// Defines a widget to manage the fullscreen state of the [App] widget.
class HomeContainer extends StatelessWidget {
  final AppModel model;

  const HomeContainer({@required this.model});

  @override
  Widget build(BuildContext context) {
    final dragOffset = ValueNotifier<double>(0);
    final isDragging = ValueNotifier<bool>(false);
    return AnimatedBuilder(
      animation: Listenable.merge([
        model.clustersModel.fullscreenStoryNotifier,
        model.peekNotifier,
        model.recentsVisibility,
        dragOffset,
      ]),
      child: GestureDetector(
        behavior: HitTestBehavior.translucent,
        child: Home(model: model),
        onVerticalDragDown: (details) =>
            _onDragDown(details, dragOffset, isDragging),
        onVerticalDragUpdate: (details) =>
            _onDragUpdate(details, dragOffset, isDragging),
        onVerticalDragEnd: (details) =>
            _onDragEnd(details, dragOffset, isDragging),
        onVerticalDragCancel: () => _onDragEnd(null, dragOffset, isDragging),
      ),
      builder: (context, child) {
        double top = model.isFullscreen && !model.peekNotifier.value
            ? -ErmineStyle.kTopBarHeight -
                ErmineStyle.kStoryTitleHeight +
                dragOffset.value
            : 0;
        double left =
            model.recentsVisibility.value ? ErmineStyle.kRecentsBarWidth : 0;
        final duration = isDragging.value
            ? Duration.zero
            : ErmineStyle.kScreenAnimationDuration;
        final curve = isDragging.value
            ? Curves.linear
            : ErmineStyle.kScreenAnimationCurve;
        return AnimatedContainer(
          transform: Matrix4.translation(Vector3(left, top, 0)),
          child: child,
          duration: duration,
          curve: curve,
        );
      },
    );
  }

  void _onDragDown(DragDownDetails details, ValueNotifier<double> offset,
      ValueNotifier<bool> isDragging) {
    if (model.isFullscreen && !model.peekNotifier.value) {
      if (details.globalPosition.dy < ErmineStyle.kTopBarHeight) {
        offset.value = details.globalPosition.dy;
        isDragging.value = true;
      }
    }
  }

  void _onDragUpdate(DragUpdateDetails details, ValueNotifier<double> offset,
      ValueNotifier<bool> isDragging) {
    if (model.isFullscreen && !model.peekNotifier.value && isDragging.value) {
      if (offset.value >
          ErmineStyle.kTopBarHeight + ErmineStyle.kStoryTitleHeight) {
        offset.value += details.delta.dy / 4;
      } else {
        offset.value += details.delta.dy;
      }
    }
  }

  void _onDragEnd(DragEndDetails details, ValueNotifier<double> offset,
      ValueNotifier<bool> isDragging) {
    if (model.isFullscreen &&
        !model.peekNotifier.value &&
        isDragging.value &&
        details != null &&
        offset.value + details.velocity.pixelsPerSecond.dy * 2 >
            ErmineStyle.kTopBarHeight + ErmineStyle.kStoryTitleHeight) {
      model.onFullscreen();
    }
    offset.value = 0;
    isDragging.value = false;
  }
}
