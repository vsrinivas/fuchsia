// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:ui';

import 'package:flutter/material.dart';

// Builds bar visualization given a value, fill amount, and maximum amount.
class ProgressBar extends StatelessWidget {
  final double value;
  final ValueChanged<double> onChange;

  const ProgressBar({this.value, this.onChange});

  @override
  Widget build(BuildContext context) {
    return Listener(
      onPointerDown: (event) => onChange?.call(_convert(context, event)),
      child: LinearProgressIndicator(
        backgroundColor: Color(0xFF262626),
        valueColor: AlwaysStoppedAnimation<Color>(Color(0xFFF2F2F2)),
        value: value,
      ),
    );
  }

  double _convert(BuildContext context, PointerDownEvent event) {
    RenderBox box = context.findRenderObject();
    if (box != null && box.hasSize) {
      final origin = box.localToGlobal(Offset.zero);
      final local = event.position - origin;
      final size = box.size;
      final value = local.dx / size.width;
      return value;
    }
    return 0;
  }
}
