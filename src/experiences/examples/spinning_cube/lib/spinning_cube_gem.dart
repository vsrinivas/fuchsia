// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:math' as math;

import 'package:flutter/material.dart';
import 'package:flutter/widgets.dart';
import 'package:vector_math/vector_math_64.dart';

const double _kGemCornerRadius = 16.0;
const double _kFaceRotation = math.pi / 2.0;
const double _kPerspectiveFieldOfViewRadians = math.pi / 6.0;
const double _kPerspectiveNearZ = 100.0;
const double _kPerspectiveAspectRatio = 1.0;
const double _kCubeScaleFactor = 50.0;
const double _kCubeAnimationYRotation = 2.0 * math.pi;
const double _kCubeAnimationXRotation = 6.0 * math.pi;

/// Creates a spinning unicolor cube with rounded corners.
class SpinningCubeGem extends StatelessWidget {
  /// Controls the spinning animation.
  final AnimationController controller;

  /// The color of the cube faces.
  final Color color;

  /// Constructor.
  const SpinningCubeGem({this.controller, this.color});

  // The six cube faces are:
  //   1. Placed in a stack and rotated and translated into different positions
  //      to form a cube.
  //   2. Manipulated into a perspective view.
  //   3. Rotated based on the animation.
  @override
  Widget build(BuildContext context) {
    return LayoutBuilder(
      builder: (BuildContext context, BoxConstraints constraints) {
        final gemSize = math.min(
          constraints.maxWidth,
          constraints.maxHeight,
        );
        final faceSize = gemSize;
        final halfFaceSize = faceSize / 2.0;
        final perspectiveFarZ = _kPerspectiveNearZ + (2.0 * faceSize);
        final cubeZ = _kPerspectiveNearZ + faceSize;

        /// We draw the cube with a 3D perspective.  This matrix manipulates the cube
        /// faces into this perspective.
        final cubePerspective = (Matrix4.diagonal3Values(
              _kCubeScaleFactor,
              _kCubeScaleFactor,
              _kCubeScaleFactor,
            ) *
            makePerspectiveMatrix(
              _kPerspectiveFieldOfViewRadians,
              _kPerspectiveAspectRatio,
              _kPerspectiveNearZ,
              perspectiveFarZ,
            ))
          ..translate(0.0, 0.0, cubeZ);

        final face = SizedBox(
          width: gemSize,
          height: gemSize,
          child: DecoratedBox(
            decoration: BoxDecoration(
              color: color,
              borderRadius: BorderRadius.circular(_kGemCornerRadius),
            ),
          ),
        );

        final rightFaceTransform = Matrix4.identity()
          ..translate(halfFaceSize)
          ..rotateY(_kFaceRotation);

        final leftFaceTransform = Matrix4.identity()
          ..translate(-halfFaceSize)
          ..rotateY(_kFaceRotation);

        final backFaceTransform = Matrix4.identity()
          ..translate(0.0, 0.0, halfFaceSize);

        final frontFaceTransform = Matrix4.identity()
          ..translate(0.0, 0.0, -halfFaceSize);

        final bottomFaceTransform = Matrix4.identity()
          ..translate(0.0, halfFaceSize)
          ..rotateX(_kFaceRotation);

        final topFaceTransform = Matrix4.identity()
          ..translate(0.0, -halfFaceSize)
          ..rotateX(_kFaceRotation);

        return RepaintBoundary(
          child: AnimatedBuilder(
            animation: controller,
            builder: (BuildContext context, Widget child) {
              final baseTransform = cubePerspective.clone()
                ..rotateY(
                  _kCubeAnimationYRotation * controller.value,
                )
                ..rotateX(
                  _kCubeAnimationXRotation * controller.value,
                );
              return Stack(
                children: <Widget>[
                  // Right face.
                  Transform(
                    alignment: FractionalOffset.center,
                    child: face,
                    transform: baseTransform * rightFaceTransform,
                  ),

                  // Left face.
                  Transform(
                    alignment: FractionalOffset.center,
                    child: face,
                    transform: baseTransform * leftFaceTransform,
                  ),

                  // Back face.
                  Transform(
                    alignment: FractionalOffset.center,
                    child: face,
                    transform: baseTransform * backFaceTransform,
                  ),

                  // Front face.
                  Transform(
                    alignment: FractionalOffset.center,
                    child: face,
                    transform: baseTransform * frontFaceTransform,
                  ),

                  // Bottom face.
                  Transform(
                    alignment: FractionalOffset.center,
                    child: face,
                    transform: baseTransform * bottomFaceTransform,
                  ),

                  // Top face.
                  Transform(
                    alignment: FractionalOffset.center,
                    child: face,
                    transform: baseTransform * topFaceTransform,
                  ),
                ],
              );
            },
          ),
        );
      },
    );
  }
}
