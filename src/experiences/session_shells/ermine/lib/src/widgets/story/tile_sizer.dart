// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:flutter/material.dart';

/// Defines a widget for sizing handle between two story tiles.
class TileSizer extends StatelessWidget {
  static const double kThickness = 15.0;
  static const _kSize = 5.0;
  static const _kMargin = 5.0;

  final bool horizontal;
  const TileSizer({Axis direction}) : horizontal = direction == Axis.horizontal;

  @override
  Widget build(BuildContext context) => FractionallySizedBox(
        widthFactor: horizontal ? 0.75 : null,
        heightFactor: horizontal ? null : 0.75,
        child: Container(
          margin: EdgeInsets.symmetric(
            horizontal: horizontal ? 0 : _kMargin,
            vertical: horizontal ? _kMargin : 0,
          ),
          width: horizontal ? null : _kSize,
          height: horizontal ? _kSize : null,
          decoration: BoxDecoration(
            color: Colors.grey,
            borderRadius: BorderRadius.all(Radius.circular(2)),
          ),
        ),
      );
}
