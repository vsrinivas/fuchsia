// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:flutter/material.dart';

/// Scales a child widget by [scale].
class ScaleWidget extends StatelessWidget {
  final double scale;
  final Widget child;

  const ScaleWidget({required this.scale, required this.child});

  @override
  Widget build(BuildContext context) {
    final ratio = scale / WidgetsBinding.instance.window.devicePixelRatio;

    return FractionallySizedBox(
      widthFactor: 1 / ratio,
      heightFactor: 1 / ratio,
      child: Transform.scale(scale: ratio, child: child),
    );
  }
}
