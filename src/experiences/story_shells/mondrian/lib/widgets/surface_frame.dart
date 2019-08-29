// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:ui' show lerpDouble;

import 'package:flutter/material.dart';
import 'package:flutter/widgets.dart';

import '../models/depth_model.dart';

const double _surfaceDepth = 250.0;

/// Frame for child views
class SurfaceFrame extends StatelessWidget {
  /// Constructor
  const SurfaceFrame(
      {Key key, this.child, this.interactable = true, this.depth = 0.0})
      : assert(-2.5 <= depth && depth <= 1.0),
        super(key: key);

  /// The child
  final Widget child;

  /// If true then ChildView hit tests will go through
  final bool interactable;

  /// How much to scale this surface [-1.0, 1.0]
  /// Negative numbers increase elevation without scaling
  final double depth;

  @override
  Widget build(BuildContext context) {
    return Container(
      alignment: FractionalOffset.center,
      child: IgnorePointer(
        child: ScopedModelDescendant<DepthModel>(
          builder: (
            BuildContext context,
            Widget child,
            DepthModel depthModel,
          ) {
            // Note: Currently if you set an elevation of 0.0 scenic will
            // merge this node with other nodes around it at the same elevation.
            // This causes black to be painted when we are displaying a
            // ChildView that hasn't painted yet.  We work around this issue by
            // always setting the elevation to something more than 0.0.  This
            // will allow 'nothing' to be painted until the child view is ready.
            double elevation = lerpDouble(
              0.01,
              _surfaceDepth,
              (depthModel.maxDepth - depth) / 2.0,
            ).clamp(0.0, _surfaceDepth);
            return PhysicalModel(
              elevation: elevation,
              color: Color(0x00000000),
              child: child,
            );
          },
          child: child,
        ),
      ),
    );
  }
}
