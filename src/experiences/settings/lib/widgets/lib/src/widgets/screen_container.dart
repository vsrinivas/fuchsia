// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:ui' show Offset, Size;

import 'package:flutter/foundation.dart' show ValueListenable;
import 'package:flutter/material.dart';
import 'package:lib.widgets/model.dart' show SpringModel;

import 'conditional_builder.dart';

/// A Container for a Widget that's position is determined by a
/// [ScreenPositionModel].
class ScreenContainer extends StatelessWidget {
  /// Determines the position of the [child].
  final ScreenPositionModel screenPositionModel;

  /// The elevation the [child] should have.
  /// Defaults to 0.0.
  final double elevation;

  /// The color to use as the background of the child.
  /// Defaults to Colors.red[900].
  final Color childBackgroundColor;

  /// The width of the gesture area for collecting drags.
  /// Defaults to 32.0.
  final double gestureWidth;

  /// The child widget to display.
  final Widget child;

  /// If true, the child will be removed completely from the Widget tree when
  /// the screen is offscreen.
  final bool removeChildWhenOffscreen;

  /// Constructor.
  const ScreenContainer({
    this.removeChildWhenOffscreen = true,
    this.screenPositionModel,
    this.elevation,
    this.childBackgroundColor,
    this.gestureWidth,
    this.child,
  });

  @override
  Widget build(BuildContext context) {
    return AnimatedBuilder(
      animation: screenPositionModel,
      child: PhysicalModel(
        color: childBackgroundColor ?? Colors.red[900],
        elevation: elevation ?? 0.0,
        child: Stack(
          children: <Widget>[
            Positioned.fill(
              child: child,
            ),
            Positioned(
              top: 0.0,
              bottom: 0.0,
              left: 0.0,
              width: gestureWidth ?? 32.0,
              child: GestureDetector(
                behavior: HitTestBehavior.translucent,
                onHorizontalDragUpdate: (DragUpdateDetails details) {
                  screenPositionModel.update(delta: details.delta.dx);
                },
                onHorizontalDragEnd: (DragEndDetails details) {
                  screenPositionModel.end(
                    horizontalPixelsPerSecond:
                        details.velocity.pixelsPerSecond.dx,
                  );
                },
              ),
            )
          ],
        ),
      ),
      builder: (BuildContext context, Widget child) {
        if (removeChildWhenOffscreen) {
          return ConditionalBuilder(
            condition: !screenPositionModel.isOffscreen,
            builder: (_) {
              return Transform.translate(
                offset: screenPositionModel.offset,
                child: child,
              );
            },
          );
        } else {
          return Transform.translate(
            offset: screenPositionModel.offset,
            child: child,
          );
        }
      },
    );
  }
}

const _resetDistance = 64.0;
const _flingMinPixelsPerSecond = 100.0;

/// Handles the position of a full screen element.  This implementation comes in
/// from the right and can be dismissed by dragging/flinging the left edge to
/// the right.
class ScreenPositionModel extends SpringModel {
  /// The screen size.
  final ValueListenable<Size> screenSize;

  /// Constructor.
  ScreenPositionModel({this.screenSize}) {
    screenSize.addListener(notifyListeners);
  }

  /// The offset the screen should have from its default position.
  Offset get offset => Offset(value, 0.0);

  /// Called by a gesture detector on the left edge of the Container this
  /// Position is associated with.
  void update({double delta}) {
    if (delta == 0.0) {
      return;
    }
    jump(value + delta);
    notifyListeners();
  }

  /// Called by a gesture detector on the left edge of the Container this
  /// Position is associated with.
  void end({double horizontalPixelsPerSecond}) {
    /// If offset greater than threshold or velocity is a fling rightward, hide.
    /// Otherwise show.
    if (value < _resetDistance ||
        horizontalPixelsPerSecond < -_flingMinPixelsPerSecond) {
      show();
    } else {
      hide();
    }
    if (value != target) {
      velocity = (horizontalPixelsPerSecond / (value - target)).abs();
      startTicking();
    }
  }

  /// True if the offset indicates the screen is offscreen.
  bool get isOffscreen => offset.dx >= screenSize.value.width;

  /// Causes the screen to be shown.
  void show() {
    target = 0.0;
  }

  /// Causes the screen to be hidden.
  void hide() {
    target = screenSize.value.width;
  }

  /// True if the screen is showing or in the process of showing.
  bool get isShowing => target == 0.0;

  /// True if the screen is hiding or in the process of hiding.
  bool get isHiding => target == screenSize.value.width;
}
